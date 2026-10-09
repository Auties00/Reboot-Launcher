#include "front_test_support.hpp"

#include <cstdio>
#include <stdexcept>

#include <boost/asio/as_tuple.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/zlib/deflate_stream.hpp>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "reboot/foundation/sha256.hpp"

namespace reboot::front::test {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

constexpr auto kUseTuple = asio::as_tuple(asio::use_awaitable);

tcp::endpoint loopback(Port port) { return {asio::ip::address_v4::loopback(), port.value}; }

template <class Stream>
asio::awaitable<void> serve_requests(std::shared_ptr<TestUpstream::Handler> handler,
                                     std::shared_ptr<std::mutex> mutex, std::vector<Request>* log, Stream& stream) {
    beast::flat_buffer buffer;
    for (;;) {
        Request request;
        auto [error, size] = co_await http::async_read(stream, buffer, request, kUseTuple);
        if (error) co_return;
        {
            const std::scoped_lock lock(*mutex);
            log->push_back(request);
        }
        UpstreamReply reply = (*handler)(request);
        if (reply.kind == UpstreamReply::Kind::Hang) {
            std::array<char, 1> byte{};
            static_cast<void>(co_await stream.async_read_some(asio::buffer(byte), kUseTuple));
            co_return;
        }
        if (reply.kind == UpstreamReply::Kind::SwitchAndEcho) {
            http::response<http::empty_body> switching{http::status::switching_protocols, 11};
            switching.set(http::field::upgrade, "websocket");
            switching.set(http::field::connection, "Upgrade");
            auto [write_error, written] = co_await http::async_write(stream, switching, kUseTuple);
            if (write_error) co_return;
            if (buffer.size() != 0) {
                static_cast<void>(co_await asio::async_write(stream, buffer.data(), kUseTuple));
                buffer.consume(buffer.size());
            }
            std::array<char, 4096> chunk{};
            for (;;) {
                auto [read_error, read] = co_await stream.async_read_some(asio::buffer(chunk), kUseTuple);
                if (read_error) co_return;
                auto [echo_error, echoed] =
                    co_await asio::async_write(stream, asio::buffer(chunk.data(), read), kUseTuple);
                if (echo_error) co_return;
            }
        }
        reply.response.prepare_payload();
        auto [write_error, written] = co_await http::async_write(stream, reply.response, kUseTuple);
        if (write_error || reply.close_after || !reply.response.keep_alive() || !request.keep_alive()) co_return;
    }
}

asio::awaitable<Reply> exchange(tcp::socket& socket, beast::flat_buffer& buffer, Request request) {
    request.prepare_payload();
    auto [write_error, written] = co_await http::async_write(socket, request, kUseTuple);
    if (write_error) co_return Reply{};
    Response response;
    auto [read_error, read] = co_await http::async_read(socket, buffer, response, kUseTuple);
    if (read_error) co_return Reply{};
    co_return Reply{response.result_int(), response.body(), static_cast<const http::fields&>(response.base())};
}

u32 crc32(std::string_view data) {
    u32 crc = 0xFFFFFFFFu;
    for (const char c : data) {
        crc ^= static_cast<u8>(c);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

}  // namespace

void InboxExecutor::post(UniqueFunction<void()> task) {
    const std::scoped_lock lock(mutex_);
    tasks_.push_back(std::move(task));
}

void InboxExecutor::post_at(SteadyTime when, UniqueFunction<void()> task) {
    const std::scoped_lock lock(mutex_);
    timed_.emplace_back(when, std::move(task));
}

void InboxExecutor::drain_into(ManualExecutor& strand) {
    std::deque<UniqueFunction<void()>> tasks;
    std::deque<std::pair<SteadyTime, UniqueFunction<void()>>> timed;
    {
        const std::scoped_lock lock(mutex_);
        tasks.swap(tasks_);
        timed.swap(timed_);
    }
    for (UniqueFunction<void()>& task : tasks) strand.post(std::move(task));
    for (auto& [when, task] : timed) strand.post_at(when, std::move(task));
}

void SocketJar::add(std::function<void()> closer) {
    const std::scoped_lock lock(mutex_);
    closers_.push_back(std::move(closer));
}

void SocketJar::close_all() {
    std::vector<std::function<void()>> closers;
    {
        const std::scoped_lock lock(mutex_);
        closers.swap(closers_);
    }
    for (const auto& close : closers) close();
}

IoThread::IoThread() : work_(asio::make_work_guard(io_)), thread_([this] { io_.run(); }) {}

void IoThread::join() {
    if (!thread_.joinable()) return;
    work_.reset();
    // Owners close their sockets first; a context that still has work after that is a leak worth seeing.
    for (int i = 0; i < 1000 && !io_.stopped(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds{5});
    if (!io_.stopped()) std::fprintf(stderr, "front tests: an io_context still had work at teardown\n");
    io_.stop();
    thread_.join();
}

IoThread::~IoThread() { join(); }

FrontHarness::FrontHarness(FrontOptions options, bool peers_supported)
    : peers(peers_supported),
      front_(std::in_place, io.io(), strand, runtime.timers(), workers, tls, runtime.requests(), peers,
             std::move(options)),
      front(*front_) {}

FrontHarness::~FrontHarness() {
    front_.reset();
    io.join();
    workers.shutdown();
}

void FrontHarness::pump() {
    for (int round = 0; round < 8; ++round) {
        strand.drain_into(runtime.strand());
        if (runtime.run_until_idle() == 0) return;
    }
}

bool FrontHarness::pump_until(const std::function<bool()>& ready) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < give_up) {
        pump();
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

void FrontHarness::advance(std::chrono::milliseconds by) {
    pump();
    runtime.advance(by);
    pump();
}

Port FrontHarness::port() const { return *front.port(); }

FrontRoute FrontHarness::embedded_route(SessionId session, const SessionKey& key) const {
    return FrontRoute{session, key, EmbeddedUpstream{}, std::nullopt};
}

SessionId session_id(u8 seed) {
    SessionId id;
    id.value.bytes.fill(seed);
    return id;
}

SessionKey session_key(u8 seed) {
    SessionKey key;
    for (std::size_t i = 0; i < key.bytes.size(); ++i) key.bytes[i] = static_cast<u8>(seed + i);
    return key;
}

UpstreamReply respond(unsigned status, std::string body, std::string content_type) {
    UpstreamReply reply;
    reply.response = Response{static_cast<http::status>(status), 11};
    reply.response.set(http::field::content_type, content_type);
    reply.response.body() = std::move(body);
    return reply;
}

TestCertificate make_test_certificate() {
    EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    X509* certificate = X509_new();
    X509_set_version(certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate), -3600);
    X509_gmtime_adj(X509_getm_notAfter(certificate), 86400);
    X509_set_pubkey(certificate, key);
    X509_NAME* name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("127.0.0.1"), -1, -1,
                               0);
    X509_set_issuer_name(certificate, name);
    X509_sign(certificate, key, EVP_sha256());

    TestCertificate out;
    out.server = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_server);
    SSL_CTX_use_certificate(out.server->native_handle(), certificate);
    SSL_CTX_use_PrivateKey(out.server->native_handle(), key);
    const int size = i2d_X509(certificate, nullptr);
    std::vector<u8> der(static_cast<std::size_t>(size));
    unsigned char* cursor = der.data();
    i2d_X509(certificate, &cursor);
    out.pin = sha256(der);
    X509_free(certificate);
    EVP_PKEY_free(key);
    return out;
}

struct TestUpstream::State {
    std::shared_ptr<Handler> handler;
    std::shared_ptr<asio::ssl::context> tls;
    std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
    std::vector<Request> requests;
    std::size_t connections = 0;
    std::shared_ptr<tcp::acceptor> acceptor;
    SocketJar jar;
};

namespace {

template <class Stream>
void track(SocketJar& jar, const std::shared_ptr<Stream>& stream, tcp::socket& (*socket_of)(Stream&)) {
    jar.add([weak = std::weak_ptr<Stream>(stream), socket_of] {
        if (const std::shared_ptr<Stream> live = weak.lock()) {
            boost::system::error_code ignored;
            socket_of(*live).close(ignored);
        }
    });
}

tcp::socket& plain_socket(tcp::socket& socket) { return socket; }
tcp::socket& tls_socket(asio::ssl::stream<tcp::socket>& stream) { return stream.next_layer(); }

asio::awaitable<void> serve_upstream_connection(std::shared_ptr<TestUpstream::State> state, tcp::socket socket) {
    if (!state->tls) {
        auto plain = std::make_shared<tcp::socket>(std::move(socket));
        track(state->jar, plain, &plain_socket);
        co_await serve_requests(state->handler, state->mutex, &state->requests, *plain);
        co_return;
    }
    auto stream = std::make_shared<asio::ssl::stream<tcp::socket>>(std::move(socket), *state->tls);
    track(state->jar, stream, &tls_socket);
    auto [error] = co_await stream->async_handshake(asio::ssl::stream_base::server, kUseTuple);
    if (error) co_return;
    co_await serve_requests(state->handler, state->mutex, &state->requests, *stream);
}

// A client socket the TestClient closes at teardown.
asio::awaitable<std::shared_ptr<tcp::socket>> open_client(SocketJar& jar, Port port) {
    auto socket = std::make_shared<tcp::socket>(co_await asio::this_coro::executor);
    track(jar, socket, &plain_socket);
    auto [error] = co_await socket->async_connect(loopback(port), kUseTuple);
    if (error) co_return nullptr;
    co_return socket;
}

}  // namespace

TestUpstream::TestUpstream(Handler handler, std::shared_ptr<asio::ssl::context> tls)
    : state_(std::make_shared<State>()) {
    state_->handler = std::make_shared<Handler>(std::move(handler));
    state_->tls = std::move(tls);
    state_->acceptor = std::make_shared<tcp::acceptor>(io_.io(), loopback(Port{0}));
    port_ = Port{state_->acceptor->local_endpoint().port()};
    asio::co_spawn(
        io_.io(),
        [state = state_]() -> asio::awaitable<void> {
            for (;;) {
                auto [error, socket] = co_await state->acceptor->async_accept(kUseTuple);
                if (error) co_return;
                {
                    const std::scoped_lock lock(*state->mutex);
                    ++state->connections;
                }
                asio::co_spawn(state->acceptor->get_executor(), serve_upstream_connection(state, std::move(socket)),
                               asio::detached);
            }
        },
        asio::detached);
}

TestUpstream::~TestUpstream() {
    asio::post(io_.io(), [state = state_] {
        boost::system::error_code ignored;
        state->acceptor->close(ignored);
        state->jar.close_all();
    });
}

std::vector<Request> TestUpstream::requests() const {
    const std::scoped_lock lock(*state_->mutex);
    return state_->requests;
}

std::size_t TestUpstream::connections() const {
    const std::scoped_lock lock(*state_->mutex);
    return state_->connections;
}

TestClient::~TestClient() {
    asio::post(io_.io(), [jar = jar_] { jar->close_all(); });
}

std::future<Reply> TestClient::send(Port port, Request request, std::function<void(Endpoint)> connected) {
    return asio::co_spawn(
        io_.io(),
        [jar = jar_, port, request = std::move(request),
         connected = std::move(connected)]() mutable -> asio::awaitable<Reply> {
            const std::shared_ptr<tcp::socket> socket = co_await open_client(*jar, port);
            if (!socket) co_return Reply{};
            if (connected) {
                boost::system::error_code ignored;
                connected(Endpoint{IpAddress::v4(0x7F000001), Port{socket->local_endpoint(ignored).port()}});
            }
            beast::flat_buffer buffer;
            co_return co_await exchange(*socket, buffer, std::move(request));
        },
        asio::use_future);
}

std::future<std::vector<Reply>> TestClient::send_all(Port port, std::vector<Request> requests) {
    return asio::co_spawn(
        io_.io(),
        [jar = jar_, port, requests = std::move(requests)]() mutable -> asio::awaitable<std::vector<Reply>> {
            std::vector<Reply> out;
            const std::shared_ptr<tcp::socket> socket = co_await open_client(*jar, port);
            if (!socket) co_return out;
            beast::flat_buffer buffer;
            for (Request& request : requests) out.push_back(co_await exchange(*socket, buffer, std::move(request)));
            co_return out;
        },
        asio::use_future);
}

std::future<std::string> TestClient::send_raw(Port port, std::string head) {
    return asio::co_spawn(
        io_.io(),
        [jar = jar_, port, head = std::move(head)]() -> asio::awaitable<std::string> {
            std::string out;
            const std::shared_ptr<tcp::socket> socket = co_await open_client(*jar, port);
            if (!socket) co_return out;
            static_cast<void>(co_await asio::async_write(*socket, asio::buffer(head), kUseTuple));
            std::array<char, 4096> chunk{};
            for (;;) {
                auto [read_error, read] = co_await socket->async_read_some(asio::buffer(chunk), kUseTuple);
                if (read_error) co_return out;
                out.append(chunk.data(), read);
            }
        },
        asio::use_future);
}

std::future<std::string> TestClient::upgrade_and_echo(Port port, std::string target, std::string message) {
    return asio::co_spawn(
        io_.io(),
        [jar = jar_, port, target = std::move(target), message = std::move(message)]() -> asio::awaitable<std::string> {
            const std::shared_ptr<tcp::socket> socket = co_await open_client(*jar, port);
            if (!socket) co_return std::string{};
            Request request{http::verb::get, target, 11};
            request.set(http::field::host, "127.0.0.1:" + std::to_string(port.value));
            request.set(http::field::upgrade, "websocket");
            request.set(http::field::connection, "Upgrade");
            request.set(http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
            request.set(http::field::sec_websocket_version, "13");
            request.set(http::field::sec_websocket_protocol, "xmpp");
            auto [write_error, written] = co_await http::async_write(*socket, request, kUseTuple);
            if (write_error) co_return std::string{};
            beast::flat_buffer buffer;
            http::response_parser<http::empty_body> parser;
            auto [head_error, head] = co_await http::async_read_header(*socket, buffer, parser, kUseTuple);
            if (head_error || parser.get().result() != http::status::switching_protocols) co_return std::string{};
            static_cast<void>(co_await asio::async_write(*socket, asio::buffer(message), kUseTuple));
            std::string echoed(message.size(), '\0');
            const std::size_t early = std::min(buffer.size(), echoed.size());
            asio::buffer_copy(asio::buffer(echoed.data(), early), buffer.data());
            auto [read_error, read] = co_await asio::async_read(
                *socket, asio::buffer(echoed.data() + early, echoed.size() - early), kUseTuple);
            if (read_error) co_return std::string{};
            co_return echoed;
        },
        asio::use_future);
}

Request get(std::string target, unsigned short port) {
    Request request{http::verb::get, target, 11};
    request.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    return request;
}

Request post_form(std::string target, unsigned short port, std::string form) {
    Request request{http::verb::post, target, 11};
    request.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    request.set(http::field::content_type, "application/x-www-form-urlencoded");
    request.body() = std::move(form);
    return request;
}

std::vector<u8> deflate_raw(std::string_view data) {
    beast::zlib::deflate_stream stream;
    std::vector<u8> out(data.size() * 2 + 64);
    beast::zlib::z_params params{};
    params.next_in = data.data();
    params.avail_in = data.size();
    params.next_out = out.data();
    params.avail_out = out.size();
    boost::system::error_code error;
    stream.write(params, beast::zlib::Flush::finish, error);
    out.resize(params.total_out);
    return out;
}

std::vector<u8> gzip(std::string_view data) {
    std::vector<u8> out{0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 0, 0xFF};
    const std::vector<u8> body = deflate_raw(data);
    out.insert(out.end(), body.begin(), body.end());
    const u32 size = static_cast<u32>(data.size());
    for (const u32 word : {crc32(data), size})
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<u8>(word >> shift));
    return out;
}

}  // namespace reboot::front::test
