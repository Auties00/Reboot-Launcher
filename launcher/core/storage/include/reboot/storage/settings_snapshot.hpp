#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/settings_values.hpp"

namespace reboot::storage {

// A session pins a copy at preflight, so later edits never reach it.
struct SettingsSnapshot {
    u64 revision = 0;
    StorageMode mode = StorageMode::ReadWrite;
    SettingsValues values;
};

}  // namespace reboot::storage
