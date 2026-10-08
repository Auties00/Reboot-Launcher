#pragma once

#include <optional>
#include <string>

#include "reboot/builds/version_source.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::builds {

struct DetectedVersion {
    GameVersion version;
    std::optional<Changelist> cl;
    VersionSource source = VersionSource::PeResource;
    // The marker tail, the catalog id or the user's text; for logs.
    std::string raw;
    // The PE file read, relative to the build root.
    std::optional<NativePath> file;

    bool operator==(const DetectedVersion&) const = default;
};

}  // namespace reboot::builds
