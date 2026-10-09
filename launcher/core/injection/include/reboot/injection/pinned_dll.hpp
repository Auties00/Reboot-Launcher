#pragma once

#include <array>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::injection {

// A DLL a session injects, with the digest the session host checks it against.
struct PinnedDll {
    NativePath path;
    std::array<u8, 32> sha256{};

    bool operator==(const PinnedDll&) const = default;
};

}  // namespace rb::injection
