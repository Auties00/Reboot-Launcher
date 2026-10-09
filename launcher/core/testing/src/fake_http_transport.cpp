#include "reboot/testing/fake_http_transport.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::testing {
namespace {

using Handler = UniqueFunction<FakeHttpResponse(const ports::HttpRequest&)>;

struct Route {
    std::string method;
    std::string url;
    bool prefix = false;
    std::vector<FakeHttpResponse> responses;
    std::size_t next = 0;
    std::shared_ptr<Handler> handler;
};

struct HttpState {
    HttpState(Executor& executor, const IClock& clock_ref) : deliver_on(executor), clock(clock_ref) {}

    mutable std::mutex mutex;
    Executor& deliver_on;
    const IClock& clock;
    std::vector<Route> routes;
    std::vector<ports::HttpRequest> requests;
    std::size_t in_flight = 0;
};

// One perform(): every scheduled step checks `done`, so on_done runs exactly once.
struct Exchange {
    std::shared_ptr<HttpState> state;
    std::string url;
    ports::HttpCallbacks callbacks;
    CancelRegistration cancel;
    std::optional<ports::StallPolicy> stall;
    std::vector<u8> body;
    std::size_t chunk_size = 0;
    std::optional<std::size_t> stall_after;
    u32 status = 200;
    bool done = false;
};

using ExchangePtr = std::shared_ptr<Exchange>;

[[nodiscard]] std::optional<std::string_view> header(const std::vector<ports::HttpHeader>& headers, std::string_view name) {
    for (const ports::HttpHeader& entry : headers)
        if (iequals_ascii(entry.name, name)) return entry.value;
    return std::nullopt;
}

// "bytes=N-", the only form a resumable download sends.
[[nodiscard]] std::optional<std::size_t> range_start(const ports::HttpRequest& request) {
    const auto value = header(request.headers, "Range");
    constexpr std::string_view kPrefix = "bytes=";
    if (!value || !value->starts_with(kPrefix) || !value->ends_with('-')) return std::nullopt;
    const std::string_view digits = value->substr(kPrefix.size(), value->size() - kPrefix.size() - 1);
    std::size_t start = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), start);
    if (ec != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
    return start;
}

void finish(const ExchangePtr& exchange, Result<ports::HttpStatus> result) {
    if (exchange->done) return;
    exchange->done = true;
    {
        const std::scoped_lock lock(exchange->state->mutex);
        --exchange->state->in_flight;
    }
    exchange->cancel.reset();
    UniqueFunction<void(Result<ports::HttpStatus>)> on_done = std::move(exchange->callbacks.on_done);
    exchange->callbacks = {};
    if (on_done) on_done(std::move(result));
}

void post_after(const ExchangePtr& exchange, std::chrono::milliseconds delay, UniqueFunction<void()> step) {
    HttpState& state = *exchange->state;
    if (delay <= std::chrono::milliseconds::zero()) {
        state.deliver_on.post(std::move(step));
    } else {
        state.deliver_on.post_at(state.clock.steady_now() + delay, std::move(step));
    }
}

// The host of an absolute URL, as the real transport names it in its errors.
[[nodiscard]] std::string host_of(std::string_view url) {
    if (const std::size_t scheme = url.find("://"); scheme != std::string_view::npos) url.remove_prefix(scheme + 3);
    url = url.substr(0, url.find_first_of("/?#"));
    if (const std::size_t at = url.rfind('@'); at != std::string_view::npos) url.remove_prefix(at + 1);
    if (url.starts_with('[')) return std::string(url.substr(1, url.find(']') - 1));
    return std::string(url.substr(0, url.rfind(':')));
}

// The ids and arguments CurlHttpTransport's HttpError diagnostics carry.
[[nodiscard]] Diagnostic net_error(const ExchangePtr& exchange, MessageId id, std::chrono::milliseconds limit) {
    return make_diag(ErrorDomain::Net, id).arg("host", host_of(exchange->url)).arg("limit", limit).retryable();
}

[[nodiscard]] Diagnostic cancelled() { return make_diag(kTestingDomain, msg::kCancelled).kind(ErrorKind::Cancelled); }

void send_body(const ExchangePtr& exchange, std::size_t offset) {
    if (exchange->done) return;
    const std::size_t limit = std::min(exchange->body.size(), exchange->stall_after.value_or(exchange->body.size()));
    if (offset >= limit) {
        if (limit == exchange->body.size()) {
            finish(exchange, ports::HttpStatus{exchange->status});
            return;
        }
        // Stalled: only the stall policy, a total timeout or a cancel ends it now.
        if (exchange->stall && exchange->stall->bytes_per_s > 0 && exchange->stall->window > std::chrono::milliseconds::zero()) {
            const auto window = exchange->stall->window;
            post_after(exchange, window,
                       [exchange, window] { finish(exchange, std::unexpected(net_error(exchange, msg::kTransferStalled, window))); });
        }
        return;
    }
    const std::size_t size = exchange->chunk_size == 0 ? limit - offset : std::min(exchange->chunk_size, limit - offset);
    const std::span<const u8> chunk(exchange->body.data() + offset, size);
    if (exchange->callbacks.on_body_chunk && !exchange->callbacks.on_body_chunk(chunk)) {
        finish(exchange, std::unexpected(cancelled()));
        return;
    }
    exchange->state->deliver_on.post([exchange, next = offset + size] { send_body(exchange, next); });
}

}  // namespace

struct FakeHttpTransport::State : HttpState {
    using HttpState::HttpState;
};

FakeHttpTransport::FakeHttpTransport(Executor& deliver_on, const IClock& clock)
    : state_(std::make_shared<State>(deliver_on, clock)) {}

FakeHttpTransport::~FakeHttpTransport() = default;

void FakeHttpTransport::perform(ports::HttpRequest request, ports::HttpCallbacks callbacks, CancelToken token) {
    auto exchange = std::make_shared<Exchange>();
    exchange->state = state_;
    exchange->url = request.url;
    exchange->callbacks = std::move(callbacks);
    exchange->stall = request.stall;

    std::optional<FakeHttpResponse> response;
    std::shared_ptr<Handler> handler;
    {
        const std::scoped_lock lock(state_->mutex);
        state_->requests.push_back(request);
        ++state_->in_flight;
        for (auto it = state_->routes.rbegin(); it != state_->routes.rend(); ++it) {
            if (it->method != request.method) continue;
            if (it->prefix ? !request.url.starts_with(it->url) : request.url != it->url) continue;
            if (it->handler) {
                handler = it->handler;
            } else {
                response = it->responses[std::min(it->next, it->responses.size() - 1)];
                ++it->next;
            }
            break;
        }
    }
    if (handler && *handler) response = (*handler)(request);

    exchange->cancel = token.on_cancel([weak = std::weak_ptr<Exchange>(exchange)](CancelReason) {
        if (const auto live = weak.lock())
            live->state->deliver_on.post([live] { finish(live, std::unexpected(cancelled())); });
    });

    if (!response) {
        state_->deliver_on.post([exchange, method = request.method] {
            finish(exchange, make_diag(kTestingDomain, msg::kNoHttpRoute)
                                 .arg("method", method)
                                 .arg("url", exchange->url)
                                 .kind(ErrorKind::NotFound)
                                 .fail());
        });
        return;
    }

    if (request.total_timeout > std::chrono::milliseconds::zero()) {
        const auto limit = request.total_timeout;
        post_after(exchange, limit,
                   [exchange, limit] { finish(exchange, std::unexpected(net_error(exchange, msg::kRequestTimeout, limit))); });
    }
    if (request.connect_timeout > std::chrono::milliseconds::zero() && response->delay > request.connect_timeout) {
        const auto limit = request.connect_timeout;
        post_after(exchange, limit,
                   [exchange, limit] { finish(exchange, std::unexpected(net_error(exchange, msg::kConnectTimeout, limit))); });
    }
    if (response->error) {
        post_after(exchange, response->delay,
                   [exchange, error = std::move(*response->error)] { finish(exchange, std::unexpected(error)); });
        return;
    }

    std::vector<ports::HttpHeader> headers = std::move(response->headers);
    exchange->status = response->status;
    exchange->body = std::move(response->body);
    exchange->chunk_size = response->chunk_size;
    exchange->stall_after = response->stall_after;
    if (const auto start = range_start(request); start && response->ranges) {
        const std::size_t total = exchange->body.size();
        if (*start >= total) {
            exchange->status = 416;
            exchange->body.clear();
        } else {
            exchange->status = 206;
            headers.push_back({"Content-Range", "bytes " + std::to_string(*start) + "-" + std::to_string(total - 1) + "/" +
                                                    std::to_string(total)});
            exchange->body.erase(exchange->body.begin(), exchange->body.begin() + static_cast<std::ptrdiff_t>(*start));
            if (exchange->stall_after) exchange->stall_after = *exchange->stall_after > *start ? *exchange->stall_after - *start : 0;
        }
    }
    if (!header(headers, "Content-Length")) headers.push_back({"Content-Length", std::to_string(exchange->body.size())});

    post_after(exchange, response->delay, [exchange, headers = std::move(headers)] {
        if (exchange->done) return;
        if (exchange->callbacks.on_headers) exchange->callbacks.on_headers(ports::HttpStatus{exchange->status}, headers);
        send_body(exchange, 0);
    });
}

void FakeHttpTransport::route(std::string method, std::string url, FakeHttpResponse response) {
    route_sequence(std::move(method), std::move(url), {std::move(response)});
}

void FakeHttpTransport::route_sequence(std::string method, std::string url, std::vector<FakeHttpResponse> responses) {
    if (responses.empty()) return;
    const std::scoped_lock lock(state_->mutex);
    state_->routes.push_back(Route{std::move(method), std::move(url), false, std::move(responses), 0, nullptr});
}

void FakeHttpTransport::route_handler(std::string method, std::string url_prefix,
                                      UniqueFunction<FakeHttpResponse(const ports::HttpRequest&)> handler) {
    const std::scoped_lock lock(state_->mutex);
    state_->routes.push_back(
        Route{std::move(method), std::move(url_prefix), true, {}, 0, std::make_shared<Handler>(std::move(handler))});
}

void FakeHttpTransport::clear_routes() {
    const std::scoped_lock lock(state_->mutex);
    state_->routes.clear();
}

std::vector<ports::HttpRequest> FakeHttpTransport::requests() const {
    const std::scoped_lock lock(state_->mutex);
    return state_->requests;
}

std::size_t FakeHttpTransport::in_flight() const {
    const std::scoped_lock lock(state_->mutex);
    return state_->in_flight;
}

}  // namespace reboot::testing
