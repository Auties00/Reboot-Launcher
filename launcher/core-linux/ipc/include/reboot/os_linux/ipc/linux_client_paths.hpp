#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace rb::os_linux::ipc {

// Covers no capability ids; IPlatformPaths for reboot_client, which may not link
// core-linux/platform. Its roots must equal os_linux::platform::XdgPaths.
class LinuxClientPaths final : public ports::IPlatformPaths {
public:
    // Resolved once: the passwd entry of geteuid() through getpwuid_r (the home directory comes
    // from it, not from $HOME), the XDG_*_HOME variables only when absolute, and the image
    // holding this code (dladdr), beside which reboot-engine ships. A missing passwd entry is
    // platform.ipc_home_unavailable and an unresolvable image platform.ipc_image_unresolved.
    [[nodiscard]] static Result<LinuxClientPaths> detect();

    // <data_home>/reboot-launcher.
    [[nodiscard]] NativePath default_data_root() const override;
    // <cache_home>/reboot-launcher.
    [[nodiscard]] NativePath default_cache_root() const override;
    // <state_home>/reboot-launcher/logs.
    [[nodiscard]] NativePath default_logs_root() const override;
    [[nodiscard]] NativePath ipc_runtime_base() const override { return runtime_base_.path; }
    // The directory of the image holding this code.
    [[nodiscard]] NativePath exe_dir() const override;
    // AppImage when $APPIMAGE names a regular file and exe_dir is under $APPDIR; Tarball when
    // exe_dir is <root>/versions/<v> beside a <root>/current symlink; Dev otherwise.
    [[nodiscard]] ports::InstallKind install_kind() const override;
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override { return std::nullopt; }

    // linux_ipc_runtime_base(uid()).
    [[nodiscard]] const IpcRuntimeBase& runtime_base() const noexcept { return runtime_base_; }
    // $XDG_DATA_HOME, default ~/.local/share.
    [[nodiscard]] const NativePath& data_home() const noexcept { return data_home_; }
    // $XDG_CACHE_HOME, default ~/.cache.
    [[nodiscard]] const NativePath& cache_home() const noexcept { return cache_home_; }
    // $XDG_STATE_HOME, default ~/.local/state.
    [[nodiscard]] const NativePath& state_home() const noexcept { return state_home_; }
    [[nodiscard]] const NativePath& home() const noexcept { return home_; }
    [[nodiscard]] const std::string& user_name() const noexcept { return user_name_; }
    // /bin/sh when the passwd entry names none.
    [[nodiscard]] const std::string& login_shell() const noexcept { return login_shell_; }
    // The AppImage file itself, for InstallKind::AppImage.
    [[nodiscard]] const std::optional<NativePath>& appimage() const noexcept { return appimage_; }
    // geteuid().
    [[nodiscard]] u32 uid() const noexcept { return uid_; }

private:
    LinuxClientPaths() = default;

    NativePath home_;
    std::string user_name_;
    std::string login_shell_;
    NativePath data_home_;
    NativePath cache_home_;
    NativePath state_home_;
    IpcRuntimeBase runtime_base_;
    NativePath exe_dir_;
    ports::InstallKind install_kind_ = ports::InstallKind::Dev;
    std::optional<NativePath> appimage_;
    u32 uid_ = 0;
};

}  // namespace rb::os_linux::ipc
