#pragma once

#include <optional>

#include "reboot/backend/backend_config.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

class BackendService;

// Strand-only and move-only; must not outlive its BackendService. Releasing it ends the session's
// ConfigureSession, and the last lease stops the backend unless the user pinned it.
class BackendLease {
public:
    BackendLease() = default;
    BackendLease(BackendLease&& other) noexcept;
    BackendLease& operator=(BackendLease&& other) noexcept;
    BackendLease(const BackendLease&) = delete;
    BackendLease& operator=(const BackendLease&) = delete;
    ~BackendLease();

    [[nodiscard]] bool active() const noexcept { return service_ != nullptr; }
    // Absent for a maintenance lease (account administration, purge).
    [[nodiscard]] const std::optional<SessionId>& session() const noexcept { return session_; }
    // The config this lease runs on; a reconfigure waits until every lease is gone.
    [[nodiscard]] const BackendConfig& config() const noexcept { return config_; }

    void release();

private:
    friend class BackendService;
    BackendLease(BackendService& service, u64 id, std::optional<SessionId> session, BackendConfig config);

    BackendService* service_ = nullptr;
    u64 id_ = 0;
    std::optional<SessionId> session_;
    BackendConfig config_;
};

}  // namespace rb::backend
