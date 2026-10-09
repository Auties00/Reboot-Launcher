#pragma once

#include "reboot/identity/account_record.hpp"

namespace rb::identity {

// EventKind::IdentityChanged, coalesced per role. Coalescing can skip a name, so a subscriber that
// keys data by account_id (backend::BackendAccounts) renames from the id it last saw for record_id.
struct IdentityChangedEvent {
    AccountRecord record;
};

}  // namespace rb::identity
