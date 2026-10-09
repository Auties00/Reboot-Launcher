#pragma once

#include <string>

#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Ask raises AccountRenameConflict when the new id already has data.
enum class RenameConflictChoice : u8 { Ask, KeepExisting, Replace };

struct AccountRenameRequest {
    std::string old_account_id;
    std::string new_account_id;
    RenameConflictChoice on_conflict = RenameConflictChoice::Ask;
};

}  // namespace rb::backend
