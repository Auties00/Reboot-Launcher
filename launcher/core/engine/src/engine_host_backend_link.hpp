#pragma once

#include <string>
#include <utility>
#include <vector>

#include "reboot/backend/backend_lease.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/game_server_config.hpp"
#include "reboot/host/host_backend_link.hpp"

namespace rb {
class Executor;
class IRandom;
}  // namespace rb

namespace rb::backend {
class BackendService;
}

namespace rb::engine {

// Capabilities: none; lets a needs_backend game server lease the backend, which host cannot reach.
// Strand-only. One session lease per host session: acquire() leases, waits for readiness,
// configures the session and mints its LaunchSecret as the service token; release() drops the
// lease, which ends the session's credentials. Only the embedded backend mints one.
class EngineHostBackendLink final : public host::IHostBackendLink {
public:
    EngineHostBackendLink(backend::BackendService& backend, Executor& strand, IRandom& random) noexcept
        : backend_(backend), strand_(strand), random_(random) {}
    EngineHostBackendLink(const EngineHostBackendLink&) = delete;
    EngineHostBackendLink& operator=(const EngineHostBackendLink&) = delete;

    void acquire(SessionId session, std::string account_id, CancelToken token,
                 UniqueFunction<void(Result<gameserver::BackendAccess>)> done) override;
    void release(SessionId session) override;

private:
    [[nodiscard]] backend::BackendLease* lease_of(const SessionId& session);

    backend::BackendService& backend_;
    Executor& strand_;
    IRandom& random_;
    std::vector<std::pair<SessionId, backend::BackendLease>> leases_;
};

}  // namespace rb::engine
