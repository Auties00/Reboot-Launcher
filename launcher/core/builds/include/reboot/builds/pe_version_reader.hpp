#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "reboot/builds/release_marker.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::builds {

class IByteSource;

inline constexpr std::size_t kVersionResourceCap = 64 * 1024;

// Capabilities: game-builds.version-detection.
// COM-free RT_VERSION access on every OS, covered by a fuzz target. Blocking; runs on the
// WorkerPool. Every offset, size and count is checked before it is followed.
// Errors (builds.pe_*) name no file; detect_version wraps them with the path it read.
class PeVersionReader {
public:
    // The first language entry of the first RT_VERSION resource, at most kVersionResourceCap bytes.
    [[nodiscard]] Result<std::vector<u8>> read_version_resource(IByteSource& source) const;

    // nullopt when the resource holds no marker.
    [[nodiscard]] Result<std::optional<ReleaseMarker>> read_marker(IByteSource& source) const;

    // Last resort over the whole file, in chunks that overlap by one marker length.
    [[nodiscard]] Result<std::optional<ReleaseMarker>> scan_for_marker(IByteSource& source,
                                                                     const CancelToken& token) const;
};

}  // namespace rb::builds
