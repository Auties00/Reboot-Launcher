#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http.hpp>

#include "asio_address.hpp"
#include "content_decoding.hpp"
#include "front_core.hpp"
#include "proxy_headers.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/front/front_answer.hpp"
#include "reboot/front/front_path.hpp"
#include "reboot/front/loopback_authority.hpp"
#include "reboot/front/peer_user.hpp"
#include "reboot/front/reboot_headers.hpp"
#include "reboot/front/ticket_exchange.hpp"
#include "reboot/front/upstream_policy.hpp"
#include "reboot/ports/net.hpp"
#include "strand_routes.hpp"
#include "tls_client.hpp"

namespace reboot::front {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

constexpr auto kUseTuple = asio::as_tuple(asio::use_awaitable);
constexpr std::size_t kChunk = 16 * 1024;
constexpr std::uint32_t kHeaderLimit = 64 * 1024;
constexpr std::string_view kContinue = "HTTP/1.1 100 Continue\r\n\r\n";

using RequestParser = http::request_parser<http::buffer_body>;
using ResponseParser = http::response_parser<http::buffer_body>;

enum class Transfer : u8 { Done, ReadFailed, WriteFailed };
enum class Head : u8 { Received, NothingReceived, Failed, TimedOut };
enum class Expired : u8 { None, Connect, Response };

// Plain TCP or TLS behind one stream type, so Beast's algorithms run on either.
class UpstreamStream {
public:
    using executor_type = asio::any_io_executor;

    UpstreamStream(const executor_type& executor, asio::ssl::context& context) : tls_(executor, context) {}

    [[nodiscard]] executor_type get_executor() noexcept { return tls_.get_executor(); }
    [[nodiscard]] tcp::socket& socket() noexcept { return tls_.next_layer(); }
    [[nodiscard]] asio::ssl::stream<tcp::socket>& tls() noexcept { return tls_; }
    void use_tls() noexcept { tls_on_ = true; }

    template <class Buffers, class Token>
    auto async_read_some(const Buffers& buffers, Token&& token) {
        if (tls_on_) return tls_.async_read_some(buffers, std::forward<Token>(token));
        return tls_.next_layer().async_read_some(buffers, std::forward<Token>(token));
    }

    template <class Buffers, class Token>
    auto async_write_some(const Buffers& buffers, Token&& token) {
        if (tls_on_) return tls_.async_write_some(buffers, std::forward<Token>(token));
        return tls_.next_layer().async_write_some(buffers, std::forward<Token>(token));
    }

private:
    asio::ssl::stream<tcp::socket> tls_;
    bool tls_on_ = false;
};

struct Upstream {
    Upstream(const asio::any_io_executor& executor, asio::ssl::context& context, UpstreamOrigin origin_in,
             bool embedded_in)
        : stream(executor, context), origin(std::move(origin_in)), embedded(embedded_in) {}

    UpstreamStream stream;
    beast::flat_buffer buffer;
    UpstreamOrigin origin;
    bool embedded;
    // The last response left the connection open and fully read.
    bool reusable = false;
};

// Where one request goes.
struct Hop {
    UpstreamOrigin origin;
    bool embedded = false;
    bool relay = false;
    std::optional<std::array<u8, 32>> pin;
};

// The ticket body holds a session secret; nothing else is wiped.
class WipeOnExit {
public:
    explicit WipeOnExit(std::vector<u8>& bytes) : bytes_(bytes) {}
    WipeOnExit(const WipeOnExit&) = delete;
    WipeOnExit& operator=(const WipeOnExit&) = delete;
    ~WipeOnExit() { secure_wipe(bytes_.data(), bytes_.size()); }

private:
    std::vector<u8>& bytes_;
};

template <class Fields>
void strip_reboot_headers(Fields& fields) {
    for (auto field = fields.begin(); field != fields.end();) {
        if (is_reboot_header(std::string_view(field->name_string())))
            field = fields.erase(field);
        else
            ++field;
    }
}

template <class T>
asio::awaitable<std::optional<T>> wait(Pending<T>& pending) {
    if (!pending.settled()) static_cast<void>(co_await pending.signal().async_wait(kUseTuple));
    co_return pending.take();
}

class Connection final : public FrontConnection, public std::enable_shared_from_this<Connection> {
public:
    Connection(std::shared_ptr<FrontCore> core, tcp::socket socket, ListenerKind kind, Port port)
        : core_(std::move(core)), executor_(socket.get_executor()), client_(std::move(socket)), kind_(kind),
          port_(port), deadline_(core_, executor_), idle_(core_, executor_) {
        boost::system::error_code ignored;
        local_ = from_asio(client_.local_endpoint(ignored));
        remote_ = from_asio(client_.remote_endpoint(ignored));
        client_.set_option(tcp::no_delay(true), ignored);
    }

    void start() {
        asio::co_spawn(executor_, run(shared_from_this()), [self = shared_from_this()](std::exception_ptr error) {
            if (error) REBOOT_LOG_ERROR(Net, "front: a connection ended on an exception");
            self->close_now();
            self->core_->table.leave(*self);
        });
    }

    void abort() override {
        asio::post(executor_, [self = shared_from_this()] { self->close_now(); });
    }

    void drain() override {
        asio::post(executor_, [self = shared_from_this()] {
            self->draining_ = true;
            if (!self->busy_) self->close_now();
        });
    }

private:
    static asio::awaitable<void> run(std::shared_ptr<Connection> self) { co_await self->serve(); }

    [[nodiscard]] const FrontOptions& options() const noexcept { return core_->options; }

    void close_upstream() {
        if (!upstream_) return;
        boost::system::error_code ignored;
        upstream_->stream.socket().close(ignored);
    }

    // Only the coroutine drops the upstream, never a callback, so no operation outlives its stream.
    void drop_upstream() {
        close_upstream();
        upstream_.reset();
    }

    void close_now() {
        if (closed_) return;
        closed_ = true;
        boost::system::error_code ignored;
        client_.shutdown(tcp::socket::shutdown_both, ignored);
        client_.close(ignored);
        close_upstream();
        if (waiting_) waiting_->abandon();
        deadline_.cancel();
        idle_.cancel();
    }

    // Closes the exchange when no byte moved since the last look; a slow but moving body keeps it alive.
    void watch_idle(std::chrono::milliseconds idle) {
        idle_.arm(idle, [this, idle, seen = moved_] {
            if (moved_ != seen)
                watch_idle(idle);
            else
                close_now();
        });
    }

    asio::awaitable<void> serve() {
        bool first = true;
        while (!closed_ && !draining_) {
            busy_ = false;
            if (in_.size() == 0) {
                deadline_.arm(first ? options().request_head : options().keep_alive_idle, [this] { close_now(); });
                auto [wait_error, size] = co_await client_.async_read_some(in_.prepare(kChunk), kUseTuple);
                deadline_.cancel();
                if (wait_error) break;
                in_.commit(size);
            }
            first = false;
            busy_ = true;
            RequestParser parser;
            parser.header_limit(kHeaderLimit);
            parser.body_limit(boost::none);
            deadline_.arm(options().request_head, [this] { close_now(); });
            auto [head_error, head_size] = co_await http::async_read_header(client_, in_, parser, kUseTuple);
            deadline_.cancel();
            if (head_error) break;
            if (!co_await handle(parser)) break;
        }
    }

    asio::awaitable<bool> handle(RequestParser& parser) {
        auto& request = parser.get();
        const unsigned version = request.version();
        const LoopbackAuthority authority{port_};
        std::optional<std::string_view> origin;
        if (request.count(http::field::origin) == 1) origin = std::string_view(request[http::field::origin]);
        if (request.count(http::field::host) != 1 ||
            !authority.allows_host(std::string_view(request[http::field::host])) ||
            request.count(http::field::origin) > 1 || !authority.allows_origin(origin))
            co_return co_await answer(FrontAnswer::Forbidden, version, nullptr);

        const std::string_view target(request.target());
        std::shared_ptr<const RouteSnapshot> route;
        std::string rest;
        std::optional<UpstreamOrigin> relay;
        if (kind_ == ListenerKind::Session) {
            std::optional<FrontPath> path = parse_front_path(target);
            if (path) route = core_->table.claim(path->key, *this);
            if (!route) co_return co_await answer(FrontAnswer::UnknownKey, version, nullptr);
            rest = std::move(path->rest);
            relay = std::move(path->relay);
        } else {
            if (target.starts_with('/')) route = core_->table.claim_legacy(*this);
            if (!route) co_return co_await answer(FrontAnswer::UnknownKey, version, nullptr);
            rest = std::string(target);
        }

        FrontBase base{"http://127.0.0.1:" + std::to_string(port_.value),
                       kind_ == ListenerKind::Session ? "/s/" + route->key.to_hex() : std::string()};
        Hop hop;
        if (relay) {
            switch (relay_verdict(*route, *relay)) {
                case RelayVerdict::Forbidden: co_return co_await answer(FrontAnswer::Forbidden, version, route.get());
                case RelayVerdict::AwaitingConsent:
                    co_return co_await answer(FrontAnswer::BadGateway, version, route.get());
                case RelayVerdict::Allowed: break;
            }
            hop = Hop{*relay, false, true, pin_for(*route, *relay)};
            base.prefix += relay_segment(*relay);
        } else if (route->configured) {
            hop = Hop{route->configured->policy.backend(), false, false, route->configured->pin};
        } else {
            const std::optional<Port> embedded = core_->table.embedded();
            if (!embedded) co_return co_await answer(FrontAnswer::BackendDown, version, route.get());
            hop = Hop{UpstreamOrigin{net::UrlScheme::Http, "127.0.0.1", *embedded}, true, false, std::nullopt};
        }
        co_return co_await forward(parser, *route, hop, std::move(rest), base);
    }

    asio::awaitable<bool> forward(RequestParser& parser, const RouteSnapshot& route, const Hop& hop, std::string rest,
                                  const FrontBase& base) {
        auto& request = parser.get();
        const unsigned version = request.version();
        const std::string method(request.method_string());
        const std::string path = rest.substr(0, rest.find('?'));
        const bool client_keep_alive = request.keep_alive();
        const bool upgrade = request.count(http::field::upgrade) != 0 &&
                             http::token_list(request[http::field::connection]).exists("upgrade");
        const bool head_request = request.method() == http::verb::head;

        request.target(rest);
        strip_reboot_headers(request);
        request.set(http::field::host, host_header(hop.origin));
        if (hop.embedded) request.set(kSessionHeader, route.key.to_hex());
        bool expect_continue = beast::iequals(request[http::field::expect], "100-continue");
        if (expect_continue) request.erase(http::field::expect);

        // The ticket grant is the one request body read before it is forwarded.
        std::vector<u8> held;
        const WipeOnExit wipe_held(held);
        bool held_all = parser.is_done();
        SecretBytes swapped;
        bool settle = false;
        // The stored login only ever goes to the route's backend, never to a relayed origin.
        if (route.tickets && !hop.relay && TicketExchange::applies(method, rest)) {
            if (!parser.is_done()) {
                if (expect_continue && !co_await send_continue()) co_return false;
                expect_continue = false;
                const std::optional<bool> complete = co_await read_capped(client_, in_, parser, held, kLearnBodyCap);
                if (!complete) co_return false;
                held_all = *complete;
            }
            if (held_all) {
                std::optional<TicketSwap> swap = co_await swap_ticket(route.session, held);
                if (!swap) co_return false;
                if (swap->outcome == TicketSwapOutcome::Refused)
                    co_return co_await answer_refused(version, client_keep_alive, route, method, path);
                if (swap->outcome == TicketSwapOutcome::Swapped) {
                    swapped = std::move(swap->body);
                    settle = true;
                }
            }
        }
        if (expect_continue && !parser.is_done() && !co_await send_continue()) co_return false;

        const std::span<const u8> body = settle ? std::span<const u8>(swapped.reveal()) : std::span<const u8>(held);
        const bool set_length = held_all && (settle || !held.empty());
        const FrontAnswer unreachable = hop.embedded ? FrontAnswer::BackendDown : FrontAnswer::BadGateway;
        std::optional<ResponseParser> response;
        for (int attempt = 0;; ++attempt) {
            const std::optional<FrontAnswer> refusal = co_await obtain(hop, route, attempt == 0 && held_all);
            if (refusal) {
                if (settle) post_settle(route.session, std::nullopt);
                co_return co_await answer(*refusal, version, &route);
            }
            const bool reused = upstream_reused_;
            const Transfer sent =
                co_await relay_body(upstream_->stream, request, body, held_all, set_length, client_, in_, parser);
            if (sent == Transfer::ReadFailed) {
                if (settle) post_settle(route.session, std::nullopt);
                close_now();
                co_return false;
            }
            Head got = Head::Failed;
            if (sent == Transfer::Done) {
                got = co_await read_response_head(response, head_request);
                if (got == Head::Received) break;
            }
            // A kept-alive connection the upstream closed meanwhile: the request never reached it.
            const bool retry = attempt == 0 && reused && held_all && !closed_ &&
                               (sent == Transfer::WriteFailed || got == Head::NothingReceived);
            drop_upstream();
            if (!retry) {
                if (settle) post_settle(route.session, std::nullopt);
                co_return co_await answer(got == Head::TimedOut ? FrontAnswer::GatewayTimeout : unreachable, version,
                                          &route);
            }
        }

        auto& result = response->get();
        const unsigned status = result.result_int();
        if (settle) post_settle(route.session, status);
        if (const auto location = result.find(http::field::location); location != result.end())
            if (std::optional<std::string> mapped =
                    rewrite_location(std::string_view(location->value()), hop.origin, base))
                result.set(http::field::location, *mapped);

        if (upgrade && status == 101) {
            http::response<http::empty_body> switching{http::response_header<>(result.base())};
            auto [error, size] = co_await http::async_write(client_, switching, kUseTuple);
            log_exchange(route, method, path, hop, status);
            if (!error) co_await tunnel();
            close_now();
            co_return false;
        }

        std::vector<u8> learned;
        bool learned_all = false;
        if (route.configured && !hop.relay && status == 200 && request.method() == http::verb::get &&
            UpstreamPolicy::learns_from(path)) {
            const std::optional<bool> complete =
                co_await read_capped(upstream_->stream, upstream_->buffer, *response, learned, kLearnBodyCap);
            if (!complete) {
                drop_upstream();
                co_return co_await answer(FrontAnswer::BadGateway, version, &route);
            }
            learned_all = *complete;
            if (learned_all) {
                std::optional<std::vector<u8>> decoded =
                    decode_content(std::string_view(result[http::field::content_encoding]), learned, kLearnBodyCap);
                // Forwarded only once learned, so a relay the game opens right after it is already allowed.
                if (decoded && !co_await learn_on_strand(route.session, path, std::move(*decoded))) co_return false;
            }
        }
        const Transfer relayed = co_await relay_body(client_, result, learned, learned_all,
                                                     learned_all && !learned.empty(), upstream_->stream,
                                                     upstream_->buffer, *response);
        log_exchange(route, method, path, hop, status);
        if (relayed != Transfer::Done) {
            close_now();
            co_return false;
        }
        const bool upstream_open = result.keep_alive() && !response->need_eof();
        if (upstream_open)
            upstream_->reusable = true;
        else
            drop_upstream();
        co_return client_keep_alive && upstream_open && !closed_;
    }

    // Reuses the kept-alive upstream when allowed, else connects within upstream_connect. nullopt on success.
    asio::awaitable<std::optional<FrontAnswer>> obtain(const Hop& hop, const RouteSnapshot& route, bool allow_reuse) {
        upstream_reused_ = false;
        if (closed_) co_return FrontAnswer::BadGateway;
        if (upstream_ && allow_reuse && upstream_->reusable && upstream_->origin == hop.origin &&
            upstream_->embedded == hop.embedded) {
            upstream_->reusable = false;
            upstream_reused_ = true;
            co_return std::nullopt;
        }
        drop_upstream();
        const FrontAnswer unreachable = hop.embedded ? FrontAnswer::BackendDown : FrontAnswer::BadGateway;
        expired_ = Expired::None;
        deadline_.arm(options().upstream_connect, [this] {
            expired_ = Expired::Connect;
            close_upstream();
            if (waiting_) waiting_->abandon();
        });

        std::vector<IpAddress> addresses;
        if (const std::optional<IpAddress> literal = IpAddress::parse(hop.origin.host))
            addresses.push_back(*literal);
        else if (std::optional<std::vector<IpAddress>> resolved = co_await resolve(hop.origin))
            addresses = std::move(*resolved);
        std::vector<tcp::endpoint> endpoints;
        const std::vector<Port> front_ports = core_->table.front_ports();
        for (const IpAddress& address : addresses) {
            if (route.configured &&
                !route.configured->policy.allows_connect(hop.origin, Endpoint{address, hop.origin.port}, front_ports))
                continue;
            endpoints.emplace_back(to_asio(address), hop.origin.port.value);
        }
        if (!addresses.empty() && endpoints.empty()) {
            deadline_.cancel();
            co_return FrontAnswer::Forbidden;
        }

        bool connected = false;
        if (!endpoints.empty() && !closed_ && expired_ == Expired::None) {
            upstream_ = std::make_unique<Upstream>(executor_, *core_->tls, hop.origin, hop.embedded);
            for (const tcp::endpoint& endpoint : endpoints) {
                boost::system::error_code ignored;
                upstream_->stream.socket().close(ignored);
                auto [error] = co_await upstream_->stream.socket().async_connect(endpoint, kUseTuple);
                if (!error) {
                    connected = true;
                    upstream_->stream.socket().set_option(tcp::no_delay(true), ignored);
                    break;
                }
                if (closed_ || expired_ != Expired::None) break;
            }
            if (connected && hop.origin.scheme == net::UrlScheme::Https) connected = co_await handshake(hop);
            if (!connected) drop_upstream();
        }
        deadline_.cancel();
        if (!connected) co_return unreachable;
        co_return std::nullopt;
    }

    asio::awaitable<bool> handshake(const Hop& hop) {
        SSL* ssl = upstream_->stream.tls().native_handle();
        prepare_tls(ssl, hop.origin, hop.pin.has_value());
        upstream_->stream.use_tls();
        auto [error] = co_await upstream_->stream.tls().async_handshake(asio::ssl::stream_base::client, kUseTuple);
        if (error || (hop.pin && !certificate_matches(ssl, *hop.pin))) {
            REBOOT_LOG_DEBUG(Net, "front: TLS with {} failed verification", hop.origin.to_string());
            co_return false;
        }
        core_->strand.post([routes = core_->routes, host = hop.origin.host] {
            if (const std::shared_ptr<StrandRoutes> owner = routes.lock()) owner->remember_https(host);
        });
        co_return true;
    }

    // Blocking DNS runs on the WorkerPool; empty when the name does not resolve.
    asio::awaitable<std::optional<std::vector<IpAddress>>> resolve(const UpstreamOrigin& origin) {
        auto pending = std::make_shared<Pending<std::vector<IpAddress>>>(executor_);
        waiting_ = pending;
        core_->workers.submit<std::vector<IpAddress>>(
            [&io = core_->io, host = origin.host, port = std::to_string(origin.port.value)](
                CancelToken) -> Result<std::vector<IpAddress>> {
                tcp::resolver resolver(io);
                boost::system::error_code error;
                const auto results = resolver.resolve(host, port, tcp::resolver::numeric_service, error);
                std::vector<IpAddress> out;
                if (error) return out;
                for (const auto& entry : results) {
                    const IpAddress address = from_asio(entry.endpoint().address());
                    if (std::ranges::find(out, address) == out.end()) out.push_back(address);
                }
                return out;
            },
            CancelToken{}, core_->strand,
            [deliver = pending->resolver()](Result<std::vector<IpAddress>> result) mutable {
                deliver(result ? std::move(*result) : std::vector<IpAddress>{});
            });
        std::optional<std::vector<IpAddress>> out = co_await wait(*pending);
        waiting_.reset();
        co_return out;
    }

    asio::awaitable<Head> read_response_head(std::optional<ResponseParser>& response, bool head_request) {
        expired_ = Expired::None;
        deadline_.arm(options().upstream_response, [this] {
            expired_ = Expired::Response;
            close_upstream();
        });
        for (;;) {
            response.emplace();
            response->header_limit(kHeaderLimit);
            response->body_limit(boost::none);
            if (head_request) response->skip(true);
            auto [error, size] =
                co_await http::async_read_header(upstream_->stream, upstream_->buffer, *response, kUseTuple);
            if (error) {
                deadline_.cancel();
                if (expired_ == Expired::Response) co_return Head::TimedOut;
                co_return response->got_some() || upstream_->buffer.size() != 0 ? Head::Failed
                                                                                : Head::NothingReceived;
            }
            const unsigned status = response->get().result_int();
            // Interim answers (100, 103) are the upstream's business; the game sees the final one.
            if (status < 100 || status >= 200 || status == 101) break;
        }
        deadline_.cancel();
        co_return Head::Received;
    }

    // Copies a message's head and body from `in` to `out`. The body starts with `prefix`, which is all of
    // it when `prefix_is_all`; `set_length` frames it with its size.
    template <bool isRequest, class Out, class In>
    asio::awaitable<Transfer> relay_body(Out& out, const http::message<isRequest, http::buffer_body>& source,
                                         std::span<const u8> prefix, bool prefix_is_all, bool set_length, In& in,
                                         beast::flat_buffer& in_buffer,
                                         http::parser<isRequest, http::buffer_body>& parser) {
        http::message<isRequest, http::buffer_body> message{source.base()};
        if (set_length) message.content_length(prefix.size());
        http::serializer<isRequest, http::buffer_body> serializer{message};
        watch_idle(options().body_idle);
        Transfer result = Transfer::Done;
        if (prefix_is_all) {
            message.body().data = prefix.empty() ? nullptr : const_cast<u8*>(prefix.data());
            message.body().size = prefix.size();
            message.body().more = false;
            auto [error, size] = co_await http::async_write(out, serializer, kUseTuple);
            if (error) result = Transfer::WriteFailed;
            ++moved_;
            idle_.cancel();
            co_return result;
        }

        auto [head_error, head_size] = co_await http::async_write_header(out, serializer, kUseTuple);
        if (head_error) result = Transfer::WriteFailed;
        if (result == Transfer::Done && !prefix.empty()) {
            message.body().data = const_cast<u8*>(prefix.data());
            message.body().size = prefix.size();
            message.body().more = true;
            auto [error, size] = co_await http::async_write(out, serializer, kUseTuple);
            if (error && error != http::error::need_buffer) result = Transfer::WriteFailed;
        }
        std::array<u8, kChunk> chunk{};
        while (result == Transfer::Done && !serializer.is_done()) {
            if (!parser.is_done()) {
                parser.get().body().data = chunk.data();
                parser.get().body().size = chunk.size();
                auto [read_error, read_size] = co_await http::async_read(in, in_buffer, parser, kUseTuple);
                if (read_error && read_error != http::error::need_buffer) {
                    result = Transfer::ReadFailed;
                    break;
                }
                message.body().data = chunk.data();
                message.body().size = chunk.size() - parser.get().body().size;
                message.body().more = !parser.is_done();
            } else {
                message.body().data = nullptr;
                message.body().size = 0;
                message.body().more = false;
            }
            ++moved_;
            auto [write_error, write_size] = co_await http::async_write(out, serializer, kUseTuple);
            if (write_error && write_error != http::error::need_buffer) result = Transfer::WriteFailed;
        }
        secure_wipe(chunk.data(), chunk.size());
        idle_.cancel();
        co_return result;
    }

    // Reads the body into `out` until `cap`: true when all of it fit, false when the rest still waits in `in`.
    template <bool isRequest, class In>
    asio::awaitable<std::optional<bool>> read_capped(In& in, beast::flat_buffer& buffer,
                                                     http::parser<isRequest, http::buffer_body>& parser,
                                                     std::vector<u8>& out, std::size_t cap) {
        const boost::optional<std::uint64_t> length = parser.content_length();
        if (length && *length > cap) co_return false;
        // Sized up front, so no reallocation leaves an unwiped copy of a ticket behind.
        out.reserve(length ? static_cast<std::size_t>(*length) : kChunk);
        watch_idle(options().body_idle);
        std::array<u8, kChunk> chunk{};
        std::optional<bool> result;
        while (!parser.is_done() && out.size() <= cap) {
            parser.get().body().data = chunk.data();
            parser.get().body().size = chunk.size();
            auto [error, size] = co_await http::async_read(in, buffer, parser, kUseTuple);
            if (error && error != http::error::need_buffer) break;
            out.insert(out.end(), chunk.begin(),
                       chunk.begin() + static_cast<std::ptrdiff_t>(chunk.size() - parser.get().body().size));
            ++moved_;
        }
        if (parser.is_done() || out.size() > cap) result = parser.is_done();
        secure_wipe(chunk.data(), chunk.size());
        idle_.cancel();
        co_return result;
    }

    // The peer's uid is read on the WorkerPool, then the swap runs on the strand.
    asio::awaitable<std::optional<TicketSwap>> swap_ticket(SessionId session, std::span<const u8> form) {
        auto pending = std::make_shared<Pending<TicketSwap>>(executor_);
        waiting_ = pending;
        // A grant swapped after this exchange was aborted never goes upstream, so its reservation is freed.
        auto dropped = [routes = core_->routes, &strand = core_->strand, session](TicketSwap swap) {
            if (swap.outcome != TicketSwapOutcome::Swapped) return;
            strand.post([routes, session] {
                if (const std::shared_ptr<StrandRoutes> owner = routes.lock()) owner->settle(session, std::nullopt);
            });
        };
        ports::ILoopbackPeerInspector& peers = core_->peers;
        core_->workers.submit<std::optional<u32>>(
            [&peers, local = local_, remote = remote_](CancelToken) { return peers.peer_uid(local, remote); },
            CancelToken{}, core_->strand,
            [routes = core_->routes, engine_uid = options().engine_uid, session,
             form = SecretBytes(std::vector<u8>(form.begin(), form.end())),
             deliver = pending->resolver(std::move(dropped))](Result<std::optional<u32>> uid) mutable {
                TicketSwap swap{TicketSwapOutcome::Refused, {}};
                if (const std::shared_ptr<StrandRoutes> owner = routes.lock())
                    swap = owner->swap(session, form.reveal(), classify_peer(uid, engine_uid));
                deliver(std::move(swap));
            });
        std::optional<TicketSwap> out = co_await wait(*pending);
        waiting_.reset();
        co_return out;
    }

    void post_settle(SessionId session, std::optional<u32> status) {
        core_->strand.post([routes = core_->routes, session, status] {
            if (const std::shared_ptr<StrandRoutes> owner = routes.lock()) owner->settle(session, status);
        });
    }

    asio::awaitable<bool> learn_on_strand(SessionId session, std::string path, std::vector<u8> body) {
        auto pending = std::make_shared<Pending<bool>>(executor_);
        waiting_ = pending;
        core_->strand.post([routes = core_->routes, session, path = std::move(path), body = std::move(body),
                            deliver = pending->resolver()]() mutable {
            if (const std::shared_ptr<StrandRoutes> owner = routes.lock()) owner->learn(session, path, body);
            deliver(true);
        });
        const std::optional<bool> done = co_await wait(*pending);
        waiting_.reset();
        co_return done.has_value();
    }

    // After a 101 both sides carry raw bytes until either closes or none moves for websocket_idle.
    asio::awaitable<void> tunnel() {
        if (in_.size() != 0) {
            auto [error, size] = co_await asio::async_write(upstream_->stream, in_.data(), kUseTuple);
            if (error) co_return;
            in_.consume(in_.size());
        }
        if (upstream_->buffer.size() != 0) {
            auto [error, size] = co_await asio::async_write(client_, upstream_->buffer.data(), kUseTuple);
            if (error) co_return;
            upstream_->buffer.consume(upstream_->buffer.size());
        }
        watch_idle(options().websocket_idle);
        auto other = std::make_shared<Pending<bool>>(executor_);
        asio::co_spawn(executor_, pump(shared_from_this(), upstream_->stream, client_),
                       [deliver = other->resolver()](std::exception_ptr) mutable { deliver(true); });
        co_await pump(shared_from_this(), client_, upstream_->stream);
        // The upstream stream must outlive the other direction's pending read.
        static_cast<void>(co_await wait(*other));
        idle_.cancel();
    }

    template <class From, class To>
    static asio::awaitable<void> pump(std::shared_ptr<Connection> self, From& from, To& to) {
        std::array<u8, kChunk> chunk{};
        for (;;) {
            auto [read_error, size] = co_await from.async_read_some(asio::buffer(chunk), kUseTuple);
            if (read_error) break;
            auto [write_error, written] = co_await asio::async_write(to, asio::buffer(chunk.data(), size), kUseTuple);
            if (write_error) break;
            ++self->moved_;
        }
        // Ends the other direction too.
        self->close_now();
    }

    asio::awaitable<bool> send_continue() {
        auto [error, size] = co_await asio::async_write(client_, asio::buffer(kContinue), kUseTuple);
        co_return !error;
    }

    // The front's own answer, after which the connection closes; the target is never logged, as it holds the key.
    asio::awaitable<bool> answer(FrontAnswer answer, unsigned version, const RouteSnapshot* route) {
        const auto status = static_cast<unsigned>(answer);
        REBOOT_LOG_AT(LogLevel::Debug, Net, route ? std::optional<SessionId>(route->session) : std::nullopt,
                      "front: answered {}", status);
        if (closed_) co_return false;
        http::response<http::empty_body> response{static_cast<http::status>(status), version};
        response.set(http::field::content_length, "0");
        response.keep_alive(false);
        deadline_.arm(options().body_idle, [this] { close_now(); });
        static_cast<void>(co_await http::async_write(client_, response, kUseTuple));
        deadline_.cancel();
        co_return false;
    }

    // Epic's invalid-credentials error, which the game shows as a failed login.
    asio::awaitable<bool> answer_refused(unsigned version, bool keep_alive, const RouteSnapshot& route,
                                         const std::string& method, const std::string& path) {
        http::response<http::string_body> response{http::status::bad_request, version};
        response.set(http::field::content_type, "application/json");
        response.set("X-Epic-Error-Name", kRefusedGrantErrorName);
        response.set("X-Epic-Error-Code", kRefusedGrantErrorCode);
        response.body() = std::string(kRefusedGrantBody);
        response.keep_alive(keep_alive);
        response.prepare_payload();
        deadline_.arm(options().body_idle, [this] { close_now(); });
        auto [error, size] = co_await http::async_write(client_, response, kUseTuple);
        deadline_.cancel();
        log_exchange(route, method, path, std::nullopt, kRefusedGrantStatus);
        co_return keep_alive && !error && !closed_;
    }

    void log_exchange(const RouteSnapshot& route, const std::string& method, const std::string& path,
                      const std::optional<Hop>& hop, unsigned status) const {
        if (hop && hop->relay)
            REBOOT_LOG_AT(LogLevel::Debug, Net, route.session, "front: {} {} via {} -> {}", method, path,
                          hop->origin.to_string(), status);
        else
            REBOOT_LOG_AT(LogLevel::Debug, Net, route.session, "front: {} {} -> {}", method, path, status);
    }

    std::shared_ptr<FrontCore> core_;
    asio::any_io_executor executor_;
    tcp::socket client_;
    ListenerKind kind_;
    Port port_;
    Deadline deadline_;
    Deadline idle_;
    Endpoint local_;
    Endpoint remote_;
    beast::flat_buffer in_;
    std::unique_ptr<Upstream> upstream_;
    bool upstream_reused_ = false;
    std::shared_ptr<Abandonable> waiting_;
    u64 moved_ = 0;
    Expired expired_ = Expired::None;
    bool closed_ = false;
    bool draining_ = false;
    bool busy_ = false;
};

}  // namespace

void serve_connection(const std::shared_ptr<FrontCore>& core, tcp::socket socket, ListenerKind kind, Port port) {
    auto connection = std::make_shared<Connection>(core, std::move(socket), kind, port);
    if (!core->table.enter(connection, kind)) return;
    connection->start();
}

}  // namespace reboot::front
