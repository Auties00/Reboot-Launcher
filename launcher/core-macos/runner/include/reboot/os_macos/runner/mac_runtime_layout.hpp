#pragma once

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/runner.hpp"

namespace reboot::os_macos::runner {

// Covers no capability ids (decisions macos-compat-layer, owner-2).
// The one shipped macOS runtime: CrossOver-source Wine 11.0 with DXMT as builtin DLLs.
struct MacRuntimeLayout {
    // Relative to the runtime root.
    inline static constexpr std::string_view kWineLoader = "bin/wine";
    inline static constexpr std::string_view kWindowsDllDir = "lib/wine/x86_64-windows";
    inline static constexpr std::string_view kUnixLibDir = "lib/wine/x86_64-unix";
    inline static constexpr std::array<std::string_view, 4> kDxmtDlls{"d3d11.dll", "dxgi.dll", "d3d10core.dll",
                                                                      "winemetal.dll"};
    inline static constexpr std::string_view kDxmtUnixLib = "winemetal.so";
    // Never names a DXMT DLL: an n,b override would bypass the builtin copy.
    inline static constexpr std::string_view kDllOverrides = "winemenubuilder.exe=d";

    NativePath root;
    NativePath wine;

    // Blocking, so callers run it on a worker. Checks existence only; the signed manifest pins the content.
    [[nodiscard]] static Result<MacRuntimeLayout> resolve(const NativePath& runtime_dir);

    // WINEDLLOVERRIDES=kDllOverrides.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> env() const;

    [[nodiscard]] ports::RuntimeLayout to_runtime_layout() const;
};

}  // namespace reboot::os_macos::runner
