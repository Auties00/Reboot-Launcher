#pragma once

#include <chrono>
#include <optional>

#include "reboot/backend/backend_account.hpp"

namespace reboot::backend {

// Prunes only LAN-created accounts: Remote, with no identity record, last seen before the cutoff.
struct AccountPruneFilter {
    std::chrono::system_clock::time_point last_login_before;
    // Unset prunes both roles.
    std::optional<AccountRole> role;

    bool operator==(const AccountPruneFilter&) const = default;

    // An account with no recorded login is never selected.
    [[nodiscard]] bool selects(const BackendAccount& account) const noexcept;
};

}  // namespace reboot::backend
