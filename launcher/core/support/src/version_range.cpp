#include "reboot/support/version_range.hpp"

#include <algorithm>

namespace reboot::support {

bool VersionRange::contains(const GameVersion& version, std::optional<Changelist> cl) const noexcept {
    if (version < min) return false;
    const bool below_max = max.patch ? version <= max
                                     : (version.major < max.major ||
                                        (version.major == max.major && version.minor <= max.minor));
    if (!below_max) return false;
    if (changelists.empty()) return true;
    if (!cl) return false;
    return std::ranges::any_of(changelists,
                               [&](const ChangelistRange& range) { return range.first <= *cl && *cl <= range.last; });
}

}  // namespace reboot::support
