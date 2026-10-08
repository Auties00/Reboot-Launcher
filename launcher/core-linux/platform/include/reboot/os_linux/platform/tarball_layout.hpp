#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::platform {

class XdgPaths;

// The self-installed tarball. Only bin/ and the two symlinks outlive a version.
struct TarballLayout {
    NativePath root;

    [[nodiscard]] NativePath versions_dir() const { return root / "versions"; }
    [[nodiscard]] NativePath version_dir(std::string_view version) const { return versions_dir() / version; }
    [[nodiscard]] NativePath current_link() const { return root / "current"; }
    [[nodiscard]] NativePath previous_link() const { return root / "previous"; }
    // POSIX sh. Finds the data root as XdgPaths does (REBOOT_LAUNCHER_HOME, else XDG_DATA_HOME,
    // else the passwd home). While its state/update-in-progress has `to` = current's version,
    // it flips `current` back to `previous` at updates::kMaxUpdateAttempts, else counts the
    // start there. Then it execs current/reboot-engine with its own arguments.
    [[nodiscard]] NativePath shim() const { return root / "bin" / "reboot-engine"; }
};

// How a long-lived registration starts the engine, independent of the running version.
struct EngineCommand {
    NativePath exe;
    // Arguments placed before `run ...`.
    std::vector<std::string> leading_args;
};

// Tarball: the shim. AppImage: $APPIMAGE with "engine", which the CLI at AppRun turns into an
// exec of the bundled engine. Dev has no stable entry: platform.integration_needs_user_install.
[[nodiscard]] Result<EngineCommand> stable_engine_command(const XdgPaths& paths);

}  // namespace reboot::os_linux::platform
