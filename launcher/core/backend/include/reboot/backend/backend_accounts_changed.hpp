#pragma once

#include <vector>

#include "reboot/backend/backend_account.hpp"

namespace rb::backend {

// EventKind::BackendAccountsChanged, coalesced: the whole list after each reload, so a missed
// event loses nothing.
struct BackendAccountsChanged {
    std::vector<BackendAccount> accounts;
};

}  // namespace rb::backend
