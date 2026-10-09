#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/net/our_process.hpp"

namespace boost::asio {
class io_context;
}

namespace reboot {
class EventBus;
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::net {
class PortOwnerService;
}

namespace reboot::front {

class SessionFront;

// Compiled into the custom auth DLLs this mode exists for.
inline constexpr Port kLegacyHttpPort{3551};
inline constexpr Port kLegacyXmppPort{80};

// The fixed ports to bind; only tests pick others.
struct LegacyFixedPorts {
    Port http = kLegacyHttpPort;
    Port xmpp = kLegacyXmppPort;
};

struct LegacyFixedRequest {
    // Must already have a SessionFront route.
    SessionId session;
    // Also bind :80, as injection::LegacyRequirements says: on Windows only. Without it a successful
    // open publishes XmppUnavailable{ThirdPartyAuthDll} with the session's scope.
    bool xmpp = true;
    // The engine's live children, so a port they hold reports owned_by_us.
    std::vector<net::OurProcess> ours;
};

// Capabilities: auth-backend.reverse-proxy.
// NetMode::LegacyFixed's only owner: one route unprefixed on 127.0.0.1:3551, plus :80 when asked; never
// kills a holder. Every requested port is required.
class LegacyFixedListeners {
public:
    // Accepted connections are served by `front`, under its Host and Origin checks and timeouts.
    LegacyFixedListeners(boost::asio::io_context& io, Executor& strand, WorkerPool& workers, SessionFront& front,
                         net::PortOwnerService& owners, EventBus& events, LegacyFixedPorts ports = {});
    ~LegacyFixedListeners();
    LegacyFixedListeners(const LegacyFixedListeners&) = delete;
    LegacyFixedListeners& operator=(const LegacyFixedListeners&) = delete;

    // Sync: front.legacy_fixed_in_use, unknown_session. `done` runs on the strand: net.port_busy if a holder is
    // found, else access_denied; front.listen_failed for another bind error, legacy_fixed_cancelled after `token`.
    Result<void> open(LegacyFixedRequest request, CancelToken token, UniqueFunction<void(Result<void>)> done);

    // Closes the sockets if `session` holds them. Idempotent.
    void close(SessionId session);

    [[nodiscard]] std::optional<SessionId> holder() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::front
