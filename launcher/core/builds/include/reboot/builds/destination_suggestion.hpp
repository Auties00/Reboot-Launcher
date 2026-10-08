#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::builds {

// LargestFittingVolume: <mount>/FortniteBuilds/<name> on the eligible volume with the most free
// space (the Windows default). UnderDataRoot: <data root>/builds/<name>, which stays visible to
// pressure-vessel and the Wine prefix (the macOS and Linux default).
enum class DestinationPolicy : u8 { LargestFittingVolume, UnderDataRoot };

inline constexpr std::string_view kVolumeBuildsFolder = "FortniteBuilds";

struct VolumeCandidate {
    ports::VolumeInfo volume;
    // Why the volume cannot hold the build: builds.volume_read_only, _network, _removable, _fat or
    // builds.insufficient_space. Absent when it can.
    std::optional<Diagnostic> problem;
};

struct DestinationSuggestion {
    // On the best volume even when none fits; its candidate's problem then says why.
    NativePath destination;
    // The catalog id, which is what downloads display.
    std::string name;
    // Archive plus installed size, since the archive is staged on the same volume.
    u64 required_bytes = 0;
    // False when the catalog has no installed size and `required_bytes` is the archive alone.
    bool required_bytes_complete = true;
    std::vector<VolumeCandidate> volumes;
};

}  // namespace reboot::builds
