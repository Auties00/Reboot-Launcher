#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::os_linux::ipc {

// An XDG_*_HOME value counts only when absolute, as the XDG spec requires; `fallback` otherwise.
// Lexically normal either way, as XdgPaths resolves it.
[[nodiscard]] NativePath xdg_home(std::optional<std::string_view> value, const NativePath& fallback);

// <root> when `exe_dir` is <root>/versions/<v>.
[[nodiscard]] std::optional<NativePath> tarball_root_of(const NativePath& exe_dir);

// What LinuxClientPaths::detect reads to tell the install kind.
struct InstallFacts {
    NativePath exe_dir;
    // $APPIMAGE, only when it names a regular file.
    std::optional<NativePath> appimage;
    std::optional<std::string_view> appdir;
    // <tarball_root_of(exe_dir)>/current is a symlink.
    bool tarball_current_is_link = false;
};

[[nodiscard]] ports::InstallKind classify_install(const InstallFacts& facts);

}  // namespace reboot::os_linux::ipc
