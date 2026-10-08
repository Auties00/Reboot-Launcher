#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::backend {

using AccountRole = contracts::backend::AccountRole;

// Local: an account of one of our identity records, reachable only through a launch credential.
// Remote: created by a password grant, which never selects a Local account.
enum class BackendAccountKind : u8 { Local, Remote };

// An account the embedded backend holds data for.
struct BackendAccount {
    std::string account_id;
    BackendAccountKind kind = BackendAccountKind::Remote;
    // Set for the accounts of our identity records.
    std::optional<AccountRecordId> record;
    AccountRole role = AccountRole::Client;
    std::string display_name;
    std::optional<std::chrono::system_clock::time_point> last_login;

    bool operator==(const BackendAccount&) const = default;
};

}  // namespace reboot::backend
