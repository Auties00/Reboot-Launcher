#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::os_linux::runner {

// Covers no capability ids (decisions linux-compat-layer, owner-2).
// The pinned Kron4ek Wine 11.0 amd64-wow64 build, the runner of the CI smoke tests.
struct KronWineRunner {
    // Relative to the runtime root.
    static constexpr std::string_view kWineLoader = "bin/wine";
    static constexpr std::string_view kWineServer = "bin/wineserver";
    static constexpr std::string_view kWindowsDllDir = "lib/wine/x86_64-windows";
    // Keeps Wine from writing .desktop launchers and MIME entries into the user's menus.
    static constexpr std::string_view kDllOverrides = "winemenubuilder.exe=d";

    NativePath root;
    NativePath wine;

    // Blocking, so callers run it on a worker. Errors: platform.linux_wine_missing,
    // platform.linux_runtime_read_failed.
    [[nodiscard]] static Result<KronWineRunner> resolve(const NativePath& runtime_dir);

    // WINEDLLOVERRIDES=kDllOverrides.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> env() const;

    [[nodiscard]] ports::RuntimeLayout to_runtime_layout() const;
};

}  // namespace reboot::os_linux::runner
