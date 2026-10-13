#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/builds/detected_version.hpp"

namespace rb::builds {

// The files did not settle the version; nothing was guessed.
struct NeedsUserVersion {
    // A marker tail that matched no known shape, for the prompt.
    std::optional<std::string> raw;
    // Why each candidate file gave nothing, such as builds.version_file_unreadable.
    std::vector<Diagnostic> reasons;
};

using VersionDetection = std::variant<DetectedVersion, NeedsUserVersion>;

}  // namespace rb::builds
