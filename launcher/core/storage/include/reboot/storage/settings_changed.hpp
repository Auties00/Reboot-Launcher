#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "reboot/storage/settings_values.hpp"

namespace reboot::storage {

// EventKind::SettingsChanged, coalesced: after a missed event, compare `values`, not `keys`.
struct SettingsChanged {
    u64 revision = 0;
    std::vector<std::string> keys;
    SettingsValues values;

    [[nodiscard]] std::size_t approx_bytes() const noexcept;
};

}  // namespace reboot::storage
