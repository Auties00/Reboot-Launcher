#include "reboot/builds/cl_table.hpp"

#include <algorithm>

#include "reboot/foundation/diag.hpp"

namespace reboot::builds {

std::optional<GameVersion> ClTable::lookup(Changelist changelist) const {
    const auto it = std::ranges::lower_bound(entries_, changelist.value, {}, &ClTableEntry::changelist);
    if (it == entries_.end() || it->changelist != changelist.value) return std::nullopt;
    if (const auto version = GameVersion::parse(it->version)) return *version;
    return std::nullopt;
}

}  // namespace reboot::builds
