#pragma once

#include <string>

#include "reboot/backend/backend_account.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

// RegisterAccount: makes `account_id` a Local account; replayed on every backend generation.
struct AccountRegistration {
    std::string account_id;
    AccountRecordId record;
    AccountRole role = AccountRole::Client;

    bool operator==(const AccountRegistration&) const = default;
};

}  // namespace rb::backend
