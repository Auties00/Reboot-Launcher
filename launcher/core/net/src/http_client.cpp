#include "reboot/net/http_client.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/net/http_error.hpp"
#include "url.hpp"

namespace reboot::net {

namespace {

// Written on the transport's thread until on_done, then read on the strand.
class Attempt {
public:
    Attempt(std::size_t max_body, bool secret) : max_body_(max_body), secret_(secret) {}
    Attempt(const Attempt&) = delete;
    Attempt& operator=(const Attempt&) = delete;
    ~Attempt() {
        if (secret_) secure_wipe(body_.data(), body_.capacity());
    }

    void on_headers(u32 status, const std::vector<ports::HttpHeader>& headers) {
        status_ = status;
        headers_ = headers;
        const std::string* length = find_header(headers, "Content-Length");
        if (length == nullptr) return;
        std::size_t declared = 0;
        const auto [end, ec] = std::from_chars(length->data(), length->data() + length->size(), declared);
        if (ec == std::errc{} && end == length->data() + length->size() && declared <= max_body_) grow(declared);
    }

    bool append(std::span<const u8> chunk) {
        if (chunk.size() > max_body_ - body_.size()) {
            too_large_ = true;
            return false;
        }
        if (chunk.size() > body_.capacity() - body_.size())
            grow(std::min(max_body_, std::max(body_.size() + chunk.size(), body_.capacity() * 2)));
        body_.insert(body_.end(), chunk.begin(), chunk.end());
        return true;
    }

    [[nodiscard]] u32 status() const noexcept { return status_; }
    [[nodiscard]] bool too_large() const noexcept { return too_large_; }
    [[nodiscard]] std::vector<ports::HttpHeader>& headers() noexcept { return headers_; }
    [[nodiscard]] std::vector<u8> take_body() noexcept { return std::move(body_); }

private:
    // A secret body never leaves an unwiped copy behind when its buffer moves.
    void grow(std::size_t capacity) {
        if (capacity <= body_.capacity()) return;
        if (!secret_) {
            body_.reserve(capacity);
            return;
        }
        std::vector<u8> bigger;
        bigger.reserve(capacity);
        bigger.assign(body_.begin(), body_.end());
        secure_wipe(body_.data(), body_.capacity());
        body_.swap(bigger);
    }

    std::size_t max_body_;
    bool secret_;
    u32 status_ = 0;
    std::vector<ports::HttpHeader> headers_;
    std::vector<u8> body_;
    bool too_large_ = false;
};

// Delta-seconds only; an HTTP-date needs wall time the client does not keep.
[[nodiscard]] std::optional<std::chrono::milliseconds> retry_after(const std::vector<ports::HttpHeader>& headers) {
    const std::string* value = find_header(headers, "Retry-After");
    if (value == nullptr) return std::nullopt;
    std::string_view text = *value;
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    u64 seconds = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (text.empty() || ec != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return std::chrono::seconds{static_cast<i64>(std::min<u64>(seconds, 86400))};
}

[[nodiscard]] bool retryable_status(u32 status) noexcept { return status == 429 || (status >= 500 && status <= 599); }

[[nodiscard]] std::string method_name(HttpMethod method) {
    switch (method) {
        case HttpMethod::Get: return "GET";
        case HttpMethod::Head: return "HEAD";
        case HttpMethod::Post: return "POST";
        case HttpMethod::Put: return "PUT";
        case HttpMethod::Delete: return "DELETE";
    }
    return "GET";
}

[[nodiscard]] bool is_bounded(const HttpLimits& limits) noexcept {
    if (limits.connect <= std::chrono::milliseconds::zero()) return false;
    const bool has_total = limits.total && *limits.total > std::chrono::milliseconds::zero();
    const bool has_stall = limits.stall && limits.stall->bytes_per_s > 0 && limits.stall->window > std::chrono::milliseconds::zero();
    return has_total || has_stall;
}

[[nodiscard]] ports::HttpRequest to_port_request(const HttpRequest& request, const HttpLimits& limits) {
    ports::HttpRequest out;
    out.method = method_name(request.method);
    out.url = request.url;
    out.headers = request.headers;
    out.body = request.body;
    out.connect_timeout = limits.connect;
    out.total_timeout = limits.total.value_or(std::chrono::milliseconds{0});
    out.stall = limits.stall;
    return out;
}

}  // namespace

struct HttpClient::Impl {
    struct Exchange {
        u64 id = 0;
        HttpRequest request;
        ParsedUrl url;
        HttpLimits limits;
        std::optional<HttpSecrets> secrets;
        CancelRegistration user_cancel;
        CancelSource attempt_cancel;
        TimerHandle retry_timer;
        u32 attempt = 0;
        UniqueFunction<void(Result<HttpResponse>)> done;
        UniqueFunction<void(Result<SecretHttpResponse>)> done_secret;
    };
    using ExchangePtr = std::shared_ptr<Exchange>;

    // Posted continuations hold it weakly, so none of them runs once the client is gone.
    struct Core : std::enable_shared_from_this<Core> {
        Core(ports::IHttpTransport& transport_in, HostTlsMemory& tls_in, Executor& strand_in, TimerService& timers_in,
             IRandom& random_in)
            : transport(transport_in), tls(tls_in), strand(strand_in), timers(timers_in), random(random_in) {}

        Result<ParsedUrl> validate(const HttpRequest& request, HttpLimits& limits) const {
            std::optional<ParsedUrl> url = parse_url(request.url);
            if (!url)
                return std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::InvalidUrl, .host = request.url}));
            limits = request.limits.value_or(limits_for(request.kind));
            if (!is_bounded(limits))
                return make_diag(ErrorDomain::Net, kRequestUnbounded)
                    .arg("host", url->host)
                    .kind(ErrorKind::InvalidInput)
                    .fail();
            if (Result<void> allowed = tls.check(url->scheme, url->host); !allowed)
                return std::unexpected(std::move(allowed.error()));
            return std::move(*url);
        }

        Result<void> begin(HttpRequest request, std::optional<HttpSecrets> secrets, CancelToken token,
                           UniqueFunction<void(Result<HttpResponse>)> done,
                           UniqueFunction<void(Result<SecretHttpResponse>)> done_secret) {
            HttpLimits limits;
            Result<ParsedUrl> url = validate(request, limits);
            if (!url) return std::unexpected(std::move(url.error()));

            auto exchange = std::make_shared<Exchange>();
            exchange->id = next_id++;
            exchange->request = std::move(request);
            exchange->url = std::move(*url);
            exchange->limits = limits;
            exchange->secrets = std::move(secrets);
            exchange->done = std::move(done);
            exchange->done_secret = std::move(done_secret);
            live.emplace(exchange->id, exchange);

            exchange->user_cancel = token.on_cancel([weak = weak_from_this(), id = exchange->id](CancelReason) {
                if (const std::shared_ptr<Core> self = weak.lock())
                    self->strand.post([weak, id] {
                        if (const std::shared_ptr<Core> owner = weak.lock()) owner->cancelled(id);
                    });
            });
            if (!token.cancelled()) start_attempt(exchange);
            return {};
        }

        void start_attempt(const ExchangePtr& exchange) {
            ++exchange->attempt;
            exchange->attempt_cancel = CancelSource{};
            auto attempt = std::make_shared<Attempt>(exchange->request.max_body, exchange->secrets.has_value());

            ports::HttpRequest request = to_port_request(exchange->request, exchange->limits);
            if (exchange->secrets) {
                // Growing the vector would move short values out of buffers that are never wiped.
                request.headers.reserve(request.headers.size() + exchange->secrets->headers.size());
                for (const SecretHeader& header : exchange->secrets->headers)
                    request.headers.emplace_back(ports::HttpHeader{header.name, {}}).value = header.value.reveal();
                if (!exchange->secrets->body.reveal().empty()) request.body = exchange->secrets->body.reveal();
            }

            ports::HttpCallbacks callbacks;
            callbacks.on_headers = [attempt](ports::HttpStatus status, const std::vector<ports::HttpHeader>& headers) {
                attempt->on_headers(status.code, headers);
            };
            callbacks.on_body_chunk = [attempt](std::span<const u8> chunk) { return attempt->append(chunk); };
            callbacks.on_done = [weak = weak_from_this(), id = exchange->id, number = exchange->attempt,
                                 attempt](Result<ports::HttpStatus> result) mutable {
                const std::shared_ptr<Core> self = weak.lock();
                if (!self) return;
                self->strand.post([weak, id, number, attempt = std::move(attempt), result = std::move(result)]() mutable {
                    if (const std::shared_ptr<Core> owner = weak.lock())
                        owner->attempt_done(id, number, *attempt, std::move(result));
                });
            };
            transport.perform(std::move(request), std::move(callbacks), exchange->attempt_cancel.token());
        }

        void attempt_done(u64 id, u32 number, Attempt& attempt, Result<ports::HttpStatus> result) {
            const auto it = live.find(id);
            if (it == live.end() || it->second->attempt != number) return;
            const ExchangePtr exchange = it->second;
            const bool may_retry = is_idempotent(exchange->request.method) &&
                                   exchange->attempt < std::max<u32>(exchange->request.retry.max_attempts, 1);

            if (!result) {
                if (attempt.too_large()) {
                    finish(exchange, std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::ResponseTooLarge,
                                                                             .host = exchange->url.host,
                                                                             .byte_limit = exchange->request.max_body})));
                    return;
                }
                if (result.error().retryable && may_retry) {
                    retry_after_delay(exchange, backoff(*exchange));
                    return;
                }
                finish(exchange, std::unexpected(std::move(result.error())));
                return;
            }

            if (exchange->url.scheme == UrlScheme::Https) tls.remember_https(exchange->url.host);
            const u32 status = attempt.status() != 0 ? attempt.status() : result->code;
            if (retryable_status(status) && may_retry) {
                std::chrono::milliseconds delay = backoff(*exchange);
                const std::optional<std::chrono::milliseconds> asked = retry_after(attempt.headers());
                if (!asked || *asked <= exchange->request.retry.max_retry_after) {
                    if (asked) delay = *asked;
                    retry_after_delay(exchange, delay);
                    return;
                }
            }
            finish(exchange, Answer{status, std::move(attempt.headers()), attempt.take_body()});
        }

        [[nodiscard]] std::chrono::milliseconds backoff(const Exchange& exchange) {
            const RetryPolicy& policy = exchange.request.retry;
            const u32 doublings = std::min<u32>(exchange.attempt - 1, 20);
            const i64 base = policy.first_delay.count() * (i64{1} << doublings);
            const i64 spread = base * static_cast<i64>(std::min<u32>(policy.jitter_percent, 100)) / 100;
            if (spread <= 0) return std::chrono::milliseconds{base};
            const auto bytes = random_bytes<8>(random);
            u64 value = 0;
            for (const u8 byte : bytes) value = (value << 8) | byte;
            const i64 offset = static_cast<i64>(value % static_cast<u64>(2 * spread + 1)) - spread;
            return std::chrono::milliseconds{std::max<i64>(0, base + offset)};
        }

        void retry_after_delay(const ExchangePtr& exchange, std::chrono::milliseconds delay) {
            exchange->retry_timer = timers.after(delay, [weak = weak_from_this(), id = exchange->id] {
                const std::shared_ptr<Core> self = weak.lock();
                if (!self) return;
                const auto it = self->live.find(id);
                if (it != self->live.end()) self->start_attempt(it->second);
            });
        }

        void cancelled(u64 id) {
            const auto it = live.find(id);
            if (it == live.end()) return;
            const ExchangePtr exchange = it->second;
            exchange->attempt_cancel.cancel(CancelReason::User);
            finish(exchange, std::unexpected(to_diagnostic(HttpError{.code = HttpErrorCode::Cancelled, .host = exchange->url.host})));
        }

        struct Answer {
            u32 status = 0;
            std::vector<ports::HttpHeader> headers;
            std::vector<u8> body;
        };

        void finish(const ExchangePtr& exchange, std::expected<Answer, Diagnostic> outcome) {
            live.erase(exchange->id);
            exchange->retry_timer.cancel();
            exchange->user_cancel.reset();
            if (exchange->done_secret) {
                UniqueFunction<void(Result<SecretHttpResponse>)> done = std::move(exchange->done_secret);
                if (!outcome) {
                    done(std::unexpected(std::move(outcome.error())));
                    return;
                }
                done(SecretHttpResponse{outcome->status, std::move(outcome->headers), SecretBytes(std::move(outcome->body))});
                return;
            }
            UniqueFunction<void(Result<HttpResponse>)> done = std::move(exchange->done);
            if (!outcome) {
                done(std::unexpected(std::move(outcome.error())));
                return;
            }
            done(HttpResponse{outcome->status, std::move(outcome->headers), std::move(outcome->body)});
        }

        ports::IHttpTransport& transport;
        HostTlsMemory& tls;
        Executor& strand;
        TimerService& timers;
        IRandom& random;
        u64 next_id = 1;
        std::map<u64, ExchangePtr> live;
    };

    Impl(ports::IHttpTransport& transport, HostTlsMemory& tls, Executor& strand, TimerService& timers, IRandom& random)
        : core(std::make_shared<Core>(transport, tls, strand, timers, random)) {}

    ~Impl() {
        std::map<u64, ExchangePtr> live = std::move(core->live);
        for (auto& [id, exchange] : live) {
            exchange->user_cancel.reset();
            exchange->retry_timer.cancel();
            exchange->attempt_cancel.cancel(CancelReason::Shutdown);
        }
    }

    std::shared_ptr<Core> core;
};

HttpClient::HttpClient(ports::IHttpTransport& transport, HostTlsMemory& tls, Executor& strand, TimerService& timers,
                       IRandom& random)
    : impl_(std::make_unique<Impl>(transport, tls, strand, timers, random)) {}

HttpClient::~HttpClient() = default;

Result<void> HttpClient::send(HttpRequest request, CancelToken token, UniqueFunction<void(Result<HttpResponse>)> done) {
    return impl_->core->begin(std::move(request), std::nullopt, std::move(token), std::move(done), nullptr);
}

Result<void> HttpClient::send_secret(HttpRequest request, HttpSecrets secrets, CancelToken token,
                                     UniqueFunction<void(Result<SecretHttpResponse>)> done) {
    return impl_->core->begin(std::move(request), std::move(secrets), std::move(token), nullptr, std::move(done));
}

Result<void> HttpClient::stream(HttpRequest request, ports::HttpCallbacks callbacks, CancelToken token) {
    Impl::Core& core = *impl_->core;
    HttpLimits limits;
    Result<ParsedUrl> url = core.validate(request, limits);
    if (!url) return std::unexpected(std::move(url.error()));
    if (url->scheme == UrlScheme::Https) {
        callbacks.on_done = [inner = std::move(callbacks.on_done), weak = core.weak_from_this(),
                             host = url->host](Result<ports::HttpStatus> result) mutable {
            if (result) {
                if (const std::shared_ptr<Impl::Core> self = weak.lock())
                    self->strand.post([weak, host] {
                        if (const std::shared_ptr<Impl::Core> owner = weak.lock()) owner->tls.remember_https(host);
                    });
            }
            if (inner) inner(std::move(result));
        };
    }
    core.transport.perform(to_port_request(request, limits), std::move(callbacks), std::move(token));
    return {};
}

}  // namespace reboot::net
