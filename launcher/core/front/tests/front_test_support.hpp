#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/session_front.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/fake_loopback_peer_inspector.hpp"

namespace reboot::front::test {

namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

// Thread-safe stand-in for the engine strand: I/O and worker threads post here, and the test thread
// moves the tasks onto the DeterministicRuntime's ManualExecutor when it pumps.
class InboxExecutor final : public Executor {
public:
    void post(UniqueFunction<void()> task) override;
    void post_at(SteadyTime when, UniqueFunction<void()> task) override;
    void drain_into(ManualExecutor& strand);

private:
    std::mutex mutex_;
    std::deque<UniqueFunction<void()>> tasks_;
    std::deque<std::pair<SteadyTime, UniqueFunction<void()>>> timed_;
};

// Closes the sockets handed to it, on their I/O thread, so their io_context drains before it goes.
class SocketJar {
public:
    void add(std::function<void()> closer);
    void close_all();

private:
    std::mutex mutex_;
    std::vector<std::function<void()>> closers_;
};

// An io_context on its own thread; on destruction it waits for the context to run out of work.
class IoThread {
public:
    IoThread();
    ~IoThread();
    IoThread(const IoThread&) = delete;
    IoThread& operator=(const IoThread&) = delete;

    [[nodiscard]] boost::asio::io_context& io() noexcept { return io_; }

    // Lets the context run out of work, then ends the thread; the context itself lives on. Idempotent.
    void join();

private:
    boost::asio::io_context io_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;
    std::thread thread_;
};

// The SessionFront on manual time: the strand and timers run when the test pumps, sockets are real.
class FrontHarness {
public:
    explicit FrontHarness(FrontOptions options = {}, bool peers_supported = false);
    ~FrontHarness();
    FrontHarness(const FrontHarness&) = delete;
    FrontHarness& operator=(const FrontHarness&) = delete;

    void pump();
    // Pumps until `ready` holds, for at most 10 s of wall time.
    bool pump_until(const std::function<bool()>& ready);
    // Moves manual time; tasks the I/O side posted first are run.
    void advance(std::chrono::milliseconds by);

    template <class T>
    T await(std::future<T>& future) {
        const bool ready = pump_until([&] { return future.wait_for(std::chrono::milliseconds{1}) == std::future_status::ready; });
        if (!ready) throw std::runtime_error("timed out waiting for the front");
        return future.get();
    }

    // Advances `step` at a time until `future` is ready, for deadlines the I/O side arms asynchronously.
    template <class T>
    T await_advancing(std::future<T>& future, std::chrono::milliseconds step) {
        for (int i = 0; i < 400; ++i) {
            pump();
            if (future.wait_for(std::chrono::milliseconds{5}) == std::future_status::ready) return future.get();
            advance(step);
        }
        throw std::runtime_error("timed out waiting for the front");
    }

    [[nodiscard]] Port port() const;
    // A route to the embedded backend.
    [[nodiscard]] FrontRoute embedded_route(SessionId session, const SessionKey& key) const;

    // First, so the context outlives every task and timer callback that still holds one of its objects.
    IoThread io;
    testing::DeterministicRuntime runtime;
    InboxExecutor strand;
    WorkerPool workers{1};
    net::HostTlsMemory tls{{}, nullptr};
    testing::FakeLoopbackPeerInspector peers;

private:
    std::optional<SessionFront> front_;

public:
    SessionFront& front;
};

[[nodiscard]] SessionId session_id(u8 seed);
[[nodiscard]] SessionKey session_key(u8 seed);

// What a test upstream does with one request.
struct UpstreamReply {
    enum class Kind : u8 { Respond, Hang, SwitchAndEcho };
    Kind kind = Kind::Respond;
    Response response;
    // Closes the connection after responding, whatever the response says.
    bool close_after = false;
};

[[nodiscard]] UpstreamReply respond(unsigned status, std::string body = {}, std::string content_type = "text/plain");

// A self-signed P-256 certificate for 127.0.0.1, and the SHA-256 of its DER.
struct TestCertificate {
    std::shared_ptr<boost::asio::ssl::context> server;
    std::array<u8, 32> pin{};
};
[[nodiscard]] TestCertificate make_test_certificate();

// An HTTP/1.1 server on 127.0.0.1 that records every request it reads.
class TestUpstream {
public:
    using Handler = std::function<UpstreamReply(const Request&)>;
    struct State;

    explicit TestUpstream(Handler handler, std::shared_ptr<boost::asio::ssl::context> tls = nullptr);
    ~TestUpstream();
    TestUpstream(const TestUpstream&) = delete;
    TestUpstream& operator=(const TestUpstream&) = delete;

    [[nodiscard]] Port port() const noexcept { return port_; }
    [[nodiscard]] std::vector<Request> requests() const;
    [[nodiscard]] std::size_t connections() const;

private:
    // Declared first so the sockets in `state_` go before the io_context does.
    IoThread io_;
    std::shared_ptr<State> state_;
    Port port_{};
};

struct Reply {
    // 0 when the front closed without a response.
    unsigned status = 0;
    std::string body;
    http::fields headers;
};

// Test clients on their own I/O thread.
class TestClient {
public:
    // One request on a fresh connection; `connected` runs with the client's local endpoint first.
    std::future<Reply> send(Port port, Request request, std::function<void(Endpoint)> connected = {});
    // Several requests on one connection, one after the other.
    std::future<std::vector<Reply>> send_all(Port port, std::vector<Request> requests);
    // Writes `head` raw, then reads until the front closes; the bytes it answered with.
    std::future<std::string> send_raw(Port port, std::string head);
    // Upgrades, then writes `message` and reads back as many bytes; empty when the upgrade failed.
    std::future<std::string> upgrade_and_echo(Port port, std::string target, std::string message);

    ~TestClient();

private:
    std::shared_ptr<SocketJar> jar_ = std::make_shared<SocketJar>();
    IoThread io_;
};

[[nodiscard]] Request get(std::string target, unsigned short port);
[[nodiscard]] Request post_form(std::string target, unsigned short port, std::string form);

// Raw deflate of `data`, for building gzip and zlib bodies.
[[nodiscard]] std::vector<u8> deflate_raw(std::string_view data);
[[nodiscard]] std::vector<u8> gzip(std::string_view data);

}  // namespace reboot::front::test
