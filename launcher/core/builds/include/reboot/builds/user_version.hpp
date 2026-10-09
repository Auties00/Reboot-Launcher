#pragma once

#include <optional>

#include "reboot/foundation/version.hpp"

namespace rb::builds {

// A version a person stated: the ChooseVersion answer, an ImportRequest or a CLI --version.
struct UserVersion {
    GameVersion version;
    std::optional<Changelist> cl;

    bool operator==(const UserVersion&) const = default;
};

}  // namespace rb::builds
