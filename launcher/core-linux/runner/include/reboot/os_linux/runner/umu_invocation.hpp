#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::os_linux::runner {

// Covers no capability ids (decisions linux-compat-layer, owner-2).
// umu-launcher 1.4.4 running GE-Proton for play, with the launcher-owned UMU_FOLDERS_PATH so
// other umu frontends never touch our Steam Linux Runtime.
struct UmuInvocation {
    // Relative to the umu-launcher root; a zipapp run through its python3 shebang.
    static constexpr std::string_view kUmuRun = "umu-run";
    // Relative to the GE-Proton root.
    static constexpr std::string_view kProtonScript = "proton";
    // Selects no title-specific protonfixes; PROTONFIXES_DISABLE turns off the generic ones.
    static constexpr std::string_view kGameId = "umu-default";

    NativePath umu_run;
    NativePath proton_root;
    NativePath folders;

    // Blocking, so callers run it on a worker. Errors: platform.linux_umu_run_missing,
    // platform.linux_proton_missing, platform.linux_runtime_read_failed.
    [[nodiscard]] static Result<UmuInvocation> resolve(const NativePath& launcher_dir, const NativePath& proton_dir,
                                                       const NativePath& folders);

    // UMU_RUNTIME_UPDATE=0: only SlrSetup downloads the runtime.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> env() const;

    [[nodiscard]] ports::RuntimeLayout to_runtime_layout() const;

    // RuntimeLayout has no kind, so an umu layout is the one whose env sets PROTONPATH.
    [[nodiscard]] static bool is_umu_layout(const ports::RuntimeLayout& layout);

    // PRESSURE_VESSEL_FILESYSTEMS_RW: `inherited` then `paths`, deduplicated. pressure-vessel
    // splits on ':', so such a path fails with platform.linux_path_not_exposable.
    [[nodiscard]] static Result<std::string> filesystems_rw(std::string_view inherited,
                                                            std::span<const NativePath> paths);
};

}  // namespace reboot::os_linux::runner
