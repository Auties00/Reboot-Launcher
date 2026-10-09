#pragma once

#include "reboot/compat/runner_profile.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::compat {

class PrefixManager;

// Strand-only. Marks one session as a user of its runner's prefix, from runtime preflight to
// session end; a prefix with a lease is not idle. A moved-from lease holds nothing. The manager
// must outlive its leases.
class PrefixLease {
public:
    PrefixLease() = default;
    PrefixLease(PrefixLease&& other) noexcept;
    PrefixLease& operator=(PrefixLease&& other) noexcept;
    PrefixLease(const PrefixLease&) = delete;
    PrefixLease& operator=(const PrefixLease&) = delete;
    ~PrefixLease();

    [[nodiscard]] bool held() const noexcept { return manager_ != nullptr; }
    [[nodiscard]] RunnerKind kind() const noexcept { return kind_; }
    [[nodiscard]] SessionId session() const noexcept { return session_; }

    void release() noexcept;

private:
    friend class PrefixManager;
    PrefixLease(PrefixManager& manager, u64 id, RunnerKind kind, SessionId session);

    PrefixManager* manager_ = nullptr;
    u64 id_ = 0;
    RunnerKind kind_{};
    SessionId session_;
};

}  // namespace rb::compat
