#pragma once

#include "reboot/base/types.hpp"
#include "reboot/base/version.hpp"
#include "registry/validation.hpp"

namespace rb {

// Byte-identical to sb version_bucket(canonical()). Apart from version.hpp, which every TU includes,
// as the sb registry header brings the wire codec with it.
[[nodiscard]] inline u32 version_bucket(const GameVersion& version) {
    return sb::registry::version_bucket(version.canonical());
}

}  // namespace rb
