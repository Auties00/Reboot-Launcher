#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::os_linux::platform {

// An XDG_*_HOME value counts only when absolute, as the XDG spec requires; otherwise
// <home>/<fallback>. The result is lexically normal.
[[nodiscard]] NativePath xdg_base(std::optional<std::string_view> value, const NativePath& home,
                                  std::string_view fallback);

// XDG_RUNTIME_DIR when absolute, otherwise /tmp/reboot-launcher-<uid>.
[[nodiscard]] NativePath runtime_base_for(std::optional<std::string_view> xdg_runtime_dir, u32 uid);

struct InstallFacts {
    ports::InstallKind kind = ports::InstallKind::Dev;
    std::optional<NativePath> appimage;
    std::optional<NativePath> tarball_root;
};

// AppImage when `appimage` names a regular file and `exe_dir` is under `appdir`; Tarball when
// `exe_dir` is <root>/versions/<v> beside a <root>/current symlink; Dev otherwise.
[[nodiscard]] InstallFacts detect_install(const NativePath& exe_dir, std::optional<std::string_view> appimage,
                                          std::optional<std::string_view> appdir);

// /proc/self/exe reads "<path> (deleted)" once an update replaced the file.
[[nodiscard]] NativePath without_deleted_suffix(NativePath exe);

}  // namespace reboot::os_linux::platform
