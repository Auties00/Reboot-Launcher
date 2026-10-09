#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>

#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::builds {

// The longest tail kept; real ones are under 32 characters.
inline constexpr std::size_t kReleaseTailCap = 64;

// One match of `(X.Y.Z-(\d+)\+)?\+\+Fortnite\+Release-(.+)` in a version string.
struct ReleaseMarker {
    // The engine changelist ("4.16.0-3700114+" gives 3700114), when the string carries one.
    std::optional<Changelist> engine_cl;
    // ASCII text after "+Release-" up to the string's end: "34.10-CL-40567068", "3.5", "Cert".
    std::string tail;

    bool operator==(const ReleaseMarker&) const = default;
};

// Pure and bounded; the PE reader and the raw scan share it. Scans UTF-16LE text at even and
// odd offsets, and takes the first match whose tail is printable ASCII.
[[nodiscard]] std::optional<ReleaseMarker> find_release_marker(std::span<const u8> utf16le);

}  // namespace rb::builds
