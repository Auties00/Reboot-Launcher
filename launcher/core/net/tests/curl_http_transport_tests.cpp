#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/net/curl_http_transport.hpp"
#include "reboot/net/http_response.hpp"
#include "reboot/testing/fake_system_info.hpp"

using namespace rb;
using namespace rb::net;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

// A plain-http server on loopback: one request per connection, answered by the test's handler.
class LoopbackHttpServer {
public:
    using Handler = std::function<void(tcp::socket&, const std::string& request, LoopbackHttpServer& server)>;

    explicit LoopbackHttpServer(Handler handler)
        : handler_(std::move(handler)),
          acceptor_(io_, tcp::endpoint(asio::ip::address_v4::loopback(), 0)),
          port_(acceptor_.local_endpoint().port()),
          thread_([this] { serve(); }) {}

    ~LoopbackHttpServer() {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        released_.notify_all();
        asio::io_context poke_io;
        tcp::socket poke(poke_io);
        boost::system::error_code ignored;
        poke.connect(tcp::endpoint(asio::ip::address_v4::loopback(), port_), ignored);
        thread_.join();
        for (std::thread& connection : connections_) connection.join();
    }

    [[nodiscard]] u16 port() const noexcept { return port_; }
    [[nodiscard]] std::string url(std::string_view path) const { return "http://127.0.0.1:" + std::to_string(port_) + std::string(path); }

    [[nodiscard]] std::vector<std::string> requests() const {
        const std::scoped_lock lock(mutex_);
        return requests_;
    }

    // Holds the handler until the server shuts down or `limit` passes.
    void hold(std::chrono::milliseconds limit) {
        std::unique_lock lock(mutex_);
        released_.wait_for(lock, limit, [this] { return stopping_; });
    }

private:
    // Each connection gets its own thread, so a held one does not block the next.
    void serve() {
        while (true) {
            auto socket = std::make_shared<tcp::socket>(io_);
            boost::system::error_code error;
            acceptor_.accept(*socket, error);
            {
                const std::scoped_lock lock(mutex_);
                if (stopping_ || error) return;
            }
            connections_.emplace_back([this, socket] { answer(*socket); });
        }
    }

    void answer(tcp::socket& socket) {
        boost::system::error_code error;
        asio::streambuf buffer;
        const std::size_t head = asio::read_until(socket, buffer, "\r\n\r\n", error);
        if (error) return;
        std::string request(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
        const std::size_t length_at = request.find("Content-Length: ");
        if (length_at != std::string::npos) {
            const std::size_t length = std::stoul(request.substr(length_at + 16));
            const std::size_t have = request.size() - head;
            if (length > have) asio::read(socket, buffer, asio::transfer_exactly(length - have), error);
            request.assign(asio::buffers_begin(buffer.data()), asio::buffers_end(buffer.data()));
        }
        {
            const std::scoped_lock lock(mutex_);
            requests_.push_back(request);
        }
        handler_(socket, request, *this);
    }

    Handler handler_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    u16 port_;
    mutable std::mutex mutex_;
    std::condition_variable released_;
    bool stopping_ = false;
    std::vector<std::string> requests_;
    // Only the accepting thread adds to it; the destructor joins them after that thread ends.
    std::vector<std::thread> connections_;
    std::thread thread_;
};

void respond(tcp::socket& socket, std::string_view text) {
    boost::system::error_code ignored;
    asio::write(socket, asio::buffer(text.data(), text.size()), ignored);
}

struct Exchange {
    std::mutex mutex;
    std::optional<u32> status;
    std::vector<ports::HttpHeader> headers;
    std::string body;
    std::promise<Result<ports::HttpStatus>> done;
    std::future<Result<ports::HttpStatus>> finished = done.get_future();
    std::atomic<bool> accept_body{true};

    ports::HttpCallbacks callbacks() {
        ports::HttpCallbacks out;
        out.on_headers = [this](ports::HttpStatus code, const std::vector<ports::HttpHeader>& received) {
            const std::scoped_lock lock(mutex);
            status = code.code;
            headers = received;
        };
        out.on_body_chunk = [this](std::span<const u8> chunk) {
            const std::scoped_lock lock(mutex);
            body.append(chunk.begin(), chunk.end());
            return accept_body.load();
        };
        out.on_done = [this](Result<ports::HttpStatus> result) { done.set_value(std::move(result)); };
        return out;
    }

    Result<ports::HttpStatus> wait() {
        REQUIRE(finished.wait_for(15s) == std::future_status::ready);
        return finished.get();
    }
};

ports::HttpRequest request_to(std::string url, std::string method = "GET") {
    ports::HttpRequest request;
    request.method = std::move(method);
    request.url = std::move(url);
    request.connect_timeout = 5s;
    request.total_timeout = 10s;
    return request;
}

std::unique_ptr<CurlHttpTransport> make_transport() {
    testing::FakeSystemInfo system;
    Result<std::unique_ptr<CurlHttpTransport>> transport = CurlHttpTransport::create(system);
    REQUIRE(transport);
    return std::move(*transport);
}

}  // namespace

TEST_CASE("curl delivers status, headers and body, and sends our headers", "[net][curl]") {
    LoopbackHttpServer server([](tcp::socket& socket, const std::string&, LoopbackHttpServer&) {
        respond(socket, "HTTP/1.1 200 OK\r\nContent-Length: 11\r\nETag: \"abc\"\r\nConnection: close\r\n\r\nhello world");
    });
    const auto transport = make_transport();
    Exchange exchange;
    ports::HttpRequest request = request_to(server.url("/file"));
    request.headers.push_back({"X-Test", "yes"});
    transport->perform(std::move(request), exchange.callbacks(), {});
    const Result<ports::HttpStatus> result = exchange.wait();
    REQUIRE(result);
    CHECK(result->code == 200);
    CHECK(exchange.status == 200u);
    CHECK(exchange.body == "hello world");
    REQUIRE(find_header(exchange.headers, "etag") != nullptr);
    CHECK(*find_header(exchange.headers, "etag") == "\"abc\"");
    const std::string sent = server.requests().front();
    CHECK(sent.starts_with("GET /file HTTP/1.1\r\n"));
    CHECK(sent.find("X-Test: yes\r\n") != std::string::npos);
    CHECK(sent.find("User-Agent: RebootLauncher/") != std::string::npos);
}

TEST_CASE("curl sends request bodies with the method asked for", "[net][curl]") {
    LoopbackHttpServer server([](tcp::socket& socket, const std::string&, LoopbackHttpServer&) {
        respond(socket, "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
    });
    const auto transport = make_transport();
    for (const std::string method : {"POST", "PUT"}) {
        Exchange exchange;
        ports::HttpRequest request = request_to(server.url("/upload"), method);
        request.body = {'a', '=', '1'};
        transport->perform(std::move(request), exchange.callbacks(), {});
        const Result<ports::HttpStatus> result = exchange.wait();
        REQUIRE(result);
        CHECK(result->code == 204);
        CHECK(exchange.status == 204u);
    }
    const std::vector<std::string> sent = server.requests();
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].starts_with("POST /upload "));
    CHECK(sent[0].ends_with("\r\n\r\na=1"));
    CHECK(sent[1].starts_with("PUT /upload "));
}

TEST_CASE("curl follows a redirect and reports only the final response", "[net][curl]") {
    LoopbackHttpServer server([](tcp::socket& socket, const std::string& request, LoopbackHttpServer&) {
        if (request.starts_with("GET /old"))
            respond(socket, "HTTP/1.1 302 Found\r\nLocation: /new\r\nX-Hop: 1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        else
            respond(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
    });
    const auto transport = make_transport();
    Exchange exchange;
    transport->perform(request_to(server.url("/old")), exchange.callbacks(), {});
    const Result<ports::HttpStatus> result = exchange.wait();
    REQUIRE(result);
    CHECK(result->code == 200);
    CHECK(exchange.body == "ok");
    CHECK(find_header(exchange.headers, "X-Hop") == nullptr);
}

TEST_CASE("curl failures map to HttpError diagnostics", "[net][curl]") {
    const auto transport = make_transport();
    u16 closed = 0;
    {
        asio::io_context io;
        tcp::acceptor probe(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
        closed = probe.local_endpoint().port();
    }
    Exchange refused;
    transport->perform(request_to("http://127.0.0.1:" + std::to_string(closed) + "/"), refused.callbacks(), {});
    const Result<ports::HttpStatus> connect = refused.wait();
    REQUIRE_FALSE(connect);
    CHECK(connect.error().id == "net.connect_failed");
    CHECK(connect.error().retryable);
    CHECK(refused.status == std::nullopt);

    Exchange invalid;
    transport->perform(request_to("not a url"), invalid.callbacks(), {});
    CHECK(invalid.wait().error().id == "net.invalid_url");
}

TEST_CASE("a body below the stall floor fails with net.transfer_stalled", "[net][curl]") {
    LoopbackHttpServer server([](tcp::socket& socket, const std::string&, LoopbackHttpServer& self) {
        respond(socket, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nConnection: close\r\n\r\nabc");
        self.hold(10s);
    });
    const auto transport = make_transport();
    Exchange exchange;
    ports::HttpRequest request = request_to(server.url("/slow"));
    request.total_timeout = 0ms;
    request.stall = ports::StallPolicy{1024, 1s};
    transport->perform(std::move(request), exchange.callbacks(), {});
    const Result<ports::HttpStatus> result = exchange.wait();
    REQUIRE_FALSE(result);
    CHECK(result.error().id == "net.transfer_stalled");
    CHECK(exchange.body == "abc");
}

TEST_CASE("cancelling, a receiver abort and destroying the transport each end a transfer once", "[net][curl][race]") {
    LoopbackHttpServer server([](tcp::socket& socket, const std::string& request, LoopbackHttpServer& self) {
        if (request.starts_with("GET /body"))
            respond(socket, "HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nConnection: close\r\n\r\nabc");
        self.hold(10s);
    });
    auto transport = make_transport();

    Exchange cancelled;
    CancelSource source;
    transport->perform(request_to(server.url("/hang")), cancelled.callbacks(), source.token());
    source.cancel(CancelReason::User);
    const Result<ports::HttpStatus> result = cancelled.wait();
    REQUIRE_FALSE(result);
    CHECK(result.error().id == "net.request_cancelled");
    CHECK(result.error().kind == ErrorKind::Cancelled);

    Exchange aborted;
    aborted.accept_body = false;
    transport->perform(request_to(server.url("/body")), aborted.callbacks(), {});
    CHECK_FALSE(aborted.wait());

    Exchange orphaned;
    transport->perform(request_to(server.url("/hang")), orphaned.callbacks(), {});
    transport.reset();
    REQUIRE(orphaned.finished.wait_for(0s) == std::future_status::ready);
    CHECK(orphaned.finished.get().error().id == "net.request_cancelled");
}

TEST_CASE("a throwing receiver ends its transfer with internal.bug and the transport keeps working", "[net][curl]") {
    LoopbackHttpServer server([](tcp::socket& socket, const std::string&, LoopbackHttpServer&) {
        respond(socket, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
    });
    const auto transport = make_transport();

    Exchange throwing;
    ports::HttpCallbacks callbacks = throwing.callbacks();
    callbacks.on_body_chunk = [](std::span<const u8>) -> bool { throw std::runtime_error("receiver bug"); };
    transport->perform(request_to(server.url("/a")), std::move(callbacks), {});
    const Result<ports::HttpStatus> failed = throwing.wait();
    REQUIRE_FALSE(failed);
    CHECK(failed.error().id == "internal.bug");

    Exchange throwing_headers;
    callbacks = throwing_headers.callbacks();
    callbacks.on_headers = [](ports::HttpStatus, const std::vector<ports::HttpHeader>&) { throw std::runtime_error("receiver bug"); };
    transport->perform(request_to(server.url("/b")), std::move(callbacks), {});
    CHECK(throwing_headers.wait().error().id == "internal.bug");

    Exchange next;
    transport->perform(request_to(server.url("/c")), next.callbacks(), {});
    const Result<ports::HttpStatus> answered = next.wait();
    REQUIRE(answered);
    CHECK(next.body == "ok");
}
