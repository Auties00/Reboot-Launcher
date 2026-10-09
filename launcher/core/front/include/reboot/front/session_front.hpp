#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/front/front_route.hpp"
#include "reboot/front/session_key.hpp"

namespace boost::asio {
class io_context;
}

namespace rb {
class Executor;
class TimerService;
class UserRequestRegistry;
class WorkerPool;
}  // namespace rb

namespace rb::ports {
class ILoopbackPeerInspector;
}

namespace rb::net {
class HostTlsMemory;
}

namespace rb::front {

class LegacyFixedListeners;

struct FrontOptions {
    // ISystemInfo::ca_bundle() on macOS and Linux; nullopt on Windows loads org.openssl.winstore: explicitly.
    std::optional<NativePath> ca_bundle;
    // Required wherever ILoopbackPeerInspector answers; without it every ticket swap there is refused.
    std::optional<u32> engine_uid;
    std::chrono::milliseconds upstream_connect = std::chrono::seconds{10};
    // From the request's last byte to the upstream's response head.
    std::chrono::milliseconds upstream_response = std::chrono::seconds{30};
    // A request or response body that moves no byte for this long aborts the exchange.
    std::chrono::milliseconds body_idle = std::chrono::seconds{30};
    // Bounds a client that opens a connection and never finishes its request head.
    std::chrono::milliseconds request_head = std::chrono::seconds{30};
    std::chrono::milliseconds keep_alive_idle = std::chrono::seconds{120};
    // A relayed WebSocket that carries no frame either way for this long is closed.
    std::chrono::milliseconds websocket_idle = std::chrono::minutes{10};
};

// Capabilities: auth-backend.reverse-proxy, auth-backend.+12.
// The game's only HTTP and WebSocket origin: one 127.0.0.1 listener whose /s/<key>/ prefix picks a route.
class SessionFront {
public:
    // Deadlines run on `timers`; each exchange is logged to LogCategory::Net without its key or query.
    SessionFront(boost::asio::io_context& io, Executor& strand, TimerService& timers, WorkerPool& workers,
                 net::HostTlsMemory& tls, UserRequestRegistry& requests, ports::ILoopbackPeerInspector& peers,
                 FrontOptions options);
    ~SessionFront();
    SessionFront(const SessionFront&) = delete;
    SessionFront& operator=(const SessionFront&) = delete;

    // Binds 127.0.0.1:0. Fails with front.listen_failed.
    Result<Port> start();
    [[nodiscard]] std::optional<Port> port() const;

    // http://127.0.0.1:<port>/s/<key>/, the origin ConfigureSession and our DLL receive. Fails with front.not_started.
    [[nodiscard]] Result<std::string> origin(const SessionKey& key) const;

    // Ready.http_port of the running backend; nullopt while it is down or restarting.
    void set_embedded_backend(std::optional<Port> http_port);

    // Owns the route's UpstreamPolicy. Fails with front.not_started, route_exists, key_in_use or a TLS refusal.
    Result<void> add_route(FrontRoute route);

    // Closes the route's connections, withdraws its prompts and unregisters its key. Idempotent.
    void remove_route(SessionId session);

    // Aborts exchanges still open after `grace`; `done` runs on the strand once the socket is closed.
    void stop(std::chrono::milliseconds grace, UniqueFunction<void()> done);

private:
    // Fixed listeners hand their accepted connections to a route without a prefix.
    friend class LegacyFixedListeners;

    // Connections live on the I/O context and post ticket swaps and learned bodies to the strand.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::front
