#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/version.hpp"

namespace reboot::support {

// Inclusive.
struct ChangelistRange {
    Changelist first;
    Changelist last;

    bool operator==(const ChangelistRange&) const = default;
};

// Inclusive. A max without a patch covers every patch of its major.minor. An empty
// `changelists` covers every changelist; otherwise a build with an unknown changelist is outside.
struct VersionRange {
    GameVersion min;
    GameVersion max;
    std::vector<ChangelistRange> changelists;

    [[nodiscard]] bool contains(const GameVersion& version, std::optional<Changelist> cl) const noexcept;

    bool operator==(const VersionRange&) const = default;
};

}  // namespace reboot::support
