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

namespace reboot::backend {
class BackendService;
}

namespace reboot::engine {

// Capabilities: none; lets a needs_backend game server lease the backend, which host cannot reach.
// Strand-only. One session lease per host session: acquire() leases, waits for readiness,
// registers the account and hands back the origin and service token; release() drops the lease.
class EngineHostBackendLink final : public host::IHostBackendLink {
public:
    explicit EngineHostBackendLink(backend::BackendService& backend) noexcept : backend_(backend) {}
    EngineHostBackendLink(const EngineHostBackendLink&) = delete;
    EngineHostBackendLink& operator=(const EngineHostBackendLink&) = delete;

    void acquire(SessionId session, std::string account_id, CancelToken token,
                 UniqueFunction<void(Result<gameserver::BackendAccess>)> done) override;
    void release(SessionId session) override;

private:
    backend::BackendService& backend_;
    std::vector<std::pair<SessionId, backend::BackendLease>> leases_;
};

}  // namespace reboot::engine
