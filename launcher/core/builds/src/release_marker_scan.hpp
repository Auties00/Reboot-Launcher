#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "reboot/builds/release_marker.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::builds {

inline constexpr std::string_view kReleaseMarkerText = "++Fortnite+Release-";

struct MarkerMatch {
    ReleaseMarker marker;
    // Byte offset of "++Fortnite+Release-".
    std::size_t offset = 0;
};

// find_release_marker over matches that start at `first` or later.
[[nodiscard]] std::optional<MarkerMatch> find_release_marker_from(std::span<const u8> utf16le, std::size_t first);

}  // namespace reboot::builds
