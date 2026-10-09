#pragma once

#include <optional>
#include <string>

#include "reboot/backend/account_rename_request.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Payload of a UserRequestKind::AccountRenameConflict request.
struct AccountRenameConflictPrompt {
    std::string old_account_id;
    std::string new_account_id;
    std::optional<AccountRecordId> existing_record;
};

// The only accepted answer; Ask is refused.
struct AccountRenameConflictAnswer {
    RenameConflictChoice choice = RenameConflictChoice::KeepExisting;
};

}  // namespace rb::backend
