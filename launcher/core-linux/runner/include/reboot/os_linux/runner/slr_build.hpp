#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::os_linux::runner {

// Covers no capability ids (decision linux-compat-layer).
// The Steam Linux Runtime build that GE-Proton runs in, as installed under UMU_FOLDERS_PATH.
struct SlrBuild {
    // umu's directory for the runtime, e.g. "steamrt3" for sniper.
    std::string runtime;
    // The depot version from the runtime's VERSIONS.txt.
    std::string version;

    // umu 1.4.4's runtime for toolmanifest.vdf's require_tool_appid; nullopt for any other.
    [[nodiscard]] static std::optional<std::string_view> runtime_for(std::string_view toolmanifest);
    [[nodiscard]] static std::optional<std::string> depot_version(std::string_view versions);

    // Blocking. Errors: platform.linux_slr_runtime_unknown, platform.linux_slr_build_missing,
    // platform.linux_runtime_read_failed.
    [[nodiscard]] static Result<SlrBuild> read(const NativePath& proton_root, const NativePath& folders);

    bool operator==(const SlrBuild&) const = default;
};

}  // namespace rb::os_linux::runner
