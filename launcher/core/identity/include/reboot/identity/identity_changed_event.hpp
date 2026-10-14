#pragma once

#include "reboot/identity/identity_snapshot.hpp"

namespace rb::identity {

// EventKind::IdentityChanged, coalesced. Coalescing can skip a name, so a subscriber that keys data
// by account_id (backend::BackendAccounts) renames from the id it last saw for record_id.
struct IdentityChangedEvent {
    IdentitySnapshot identity;
};

}  // namespace rb::identity
