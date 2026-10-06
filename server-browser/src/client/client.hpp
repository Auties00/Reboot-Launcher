#pragma once

// Minimal C++ client for the rbsb/1 protocol over MsQuic. Used by sb-cli, sb-loadgen and the
// integration tests; it is also the reference for future language bindings.
//
// Threading: MsQuic delivers events on its worker threads; `on_event` is invoked there.
// Request methods are thread-safe.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "quic/msquic.hpp"
#include "wire/frame.hpp"
#include "wire/messages.hpp"

namespace sb::client {

// Process-wide MsQuic handles shared by every client connection.
class ClientRuntime {
public:
    struct Options {
        bool insecure = false;  // skip certificate validation (development only)
        std::string ca_file;
        u32 idle_timeout_ms = 60'000;
        u32 max_ack_delay_ms = 100;  // fewer ACK packets for sparse datagram streams
        u32 keepalive_ms = 0;
    };

    explicit ClientRuntime(Options opts);
    ~ClientRuntime();
    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;

    [[nodiscard]] const QUIC_API_TABLE* api() const noexcept { return lib_.get(); }
    [[nodiscard]] HQUIC registration() const noexcept { return registration_; }
    [[nodiscard]] HQUIC configuration() const noexcept { return configuration_; }

private:
    quic::Library lib_;
    HQUIC registration_ = nullptr;
    HQUIC configuration_ = nullptr;
};

struct Connected {};
struct Closed {
    std::string reason;
    u64 app_error = 0;
};
struct SnapshotEvent {
    wire::Snapshot snapshot;
};
struct DeltaEvent {
    wire::Delta delta;
    bool via_datagram = true;
};

using Event = std::variant<Connected, wire::Welcome, wire::SubOpen, SnapshotEvent, DeltaEvent, wire::QueryResult,
                           wire::ResolveResult, wire::JoinGrant, wire::HostRegistered, wire::Ack, wire::Error,
                           wire::GoAway, wire::HostStatus, Closed>;

class Client {
public:
    struct Options {
        std::string host = "127.0.0.1";
        u16 port = 443;
        wire::Role role = wire::Role::browser;
        u64 features = wire::feature::datagrams | wire::feature::zstd;
        std::string client_version = "sb-client/0.1";
        std::optional<IpAddr> local_address;  // source address (load generation)
        std::function<void(Event&)> on_event;
    };

    Client(ClientRuntime& rt, Options opts);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void connect();
    void close();
    [[nodiscard]] bool connected() const noexcept { return state_.load() == State::ready; }

    u32 subscribe(const wire::ViewSpec& view, u32 window);
    void unsubscribe(u32 sub_id);
    u32 query(const wire::ViewSpec& view, std::string text, u32 limit, wire::Bytes cursor = {});
    u32 resolve(const Uuid& id);
    u32 join(const Uuid& id, std::optional<std::string> password);
    u32 host_register(wire::HostRegister msg);
    // Use want_ack = false for high-frequency player counts.
    u32 host_update(wire::HostUpdate msg, bool want_ack);
    u32 host_unregister();
    void heartbeat();

private:
    enum class State : u8 { idle, connecting, ready, closed };

    static QUIC_STATUS QUIC_API conn_cb(HQUIC, void* ctx, QUIC_CONNECTION_EVENT* ev);
    static QUIC_STATUS QUIC_API stream_cb(HQUIC, void* ctx, QUIC_STREAM_EVENT* ev);
    void on_frame(const wire::FrameView& f, bool datagram);
    void emit(Event e);
    void send_bytes(std::vector<u8> bytes);
    template <class T>
    void send(const T& msg) {
        send_bytes(wire::frame_bytes(msg));
    }
    u32 next_req() noexcept { return next_req_.fetch_add(1, std::memory_order_relaxed); }

    struct InStream;

    ClientRuntime& rt_;
    const QUIC_API_TABLE* api_;
    Options opts_;
    HQUIC conn_ = nullptr;
    HQUIC control_ = nullptr;
    std::atomic<State> state_{State::idle};
    std::atomic<u32> next_req_{1};
    std::atomic<u32> next_sub_{1};
    std::atomic<u32> heartbeat_seq_{0};
    std::mutex send_mu_;
    std::mutex done_mu_;
    std::condition_variable done_cv_;
    bool shutdown_done_ = false;
    std::vector<std::vector<u8>> queued_;  // frames sent before the control stream existed
};

}  // namespace sb::client
