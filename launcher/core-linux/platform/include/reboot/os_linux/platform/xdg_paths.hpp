#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::os_linux::platform {

// Covers no capability ids; IPlatformPaths from the XDG base directories and the running exe.
class XdgPaths final : public ports::IPlatformPaths {
public:
    // Resolves everything once. The home directory comes from getpwuid_r(geteuid()), not $HOME,
    // so a CLI under sudo -E or a stripped service environment finds the same root. XDG_*_HOME
    // and XDG_RUNTIME_DIR count only when absolute, as the XDG spec requires. The exe comes from
    // /proc/self/exe.
    [[nodiscard]] static Result<XdgPaths> detect();

    // $XDG_DATA_HOME/reboot-launcher (default ~/.local/share/reboot-launcher).
    [[nodiscard]] NativePath default_data_root() const override;
    // $XDG_CACHE_HOME/reboot-launcher (default ~/.cache/reboot-launcher).
    [[nodiscard]] NativePath default_cache_root() const override;
    // $XDG_STATE_HOME/reboot-launcher/logs (default ~/.local/state/reboot-launcher/logs).
    [[nodiscard]] NativePath default_logs_root() const override;
    // $XDG_RUNTIME_DIR when it is absolute, otherwise /tmp/reboot-launcher-<uid>, as
    // os_linux::ipc::linux_ipc_runtime_base. Not created here: the AF_UNIX listener creates and
    // lstat-checks the 0700 directories.
    [[nodiscard]] NativePath ipc_runtime_base() const override;
    // The directory of the resolved /proc/self/exe; inside the read-only mount for an AppImage.
    [[nodiscard]] NativePath exe_dir() const override;
    // AppImage when $APPIMAGE names a regular file and exe_dir is under $APPDIR; Tarball when
    // exe_dir is <root>/versions/<v> beside a <root>/current symlink; Dev otherwise. Must agree
    // with os_linux::ipc::LinuxClientPaths.
    [[nodiscard]] ports::InstallKind install_kind() const override;
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override { return std::nullopt; }

    [[nodiscard]] const NativePath& home() const noexcept { return home_; }
    // $XDG_CONFIG_HOME (default ~/.config): autostart, systemd user units and mimeapps.list.
    [[nodiscard]] const NativePath& config_home() const noexcept { return config_home_; }
    // $XDG_DATA_HOME (default ~/.local/share): applications/ for .desktop files.
    [[nodiscard]] const NativePath& data_home() const noexcept { return data_home_; }
    [[nodiscard]] const NativePath& cache_home() const noexcept { return cache_home_; }
    [[nodiscard]] const NativePath& state_home() const noexcept { return state_home_; }
    // $XDG_RUNTIME_DIR when it is absolute; systemd's user manager listens under it.
    [[nodiscard]] const std::optional<NativePath>& runtime_dir() const noexcept { return runtime_dir_; }
    [[nodiscard]] u32 uid() const noexcept { return uid_; }
    // pw_name of the effective uid.
    [[nodiscard]] const std::string& user_name() const noexcept { return user_name_; }
    // The AppImage file itself, for InstallKind::AppImage.
    [[nodiscard]] const std::optional<NativePath>& appimage() const noexcept { return appimage_; }
    // <root> of the tarball layout, for InstallKind::Tarball.
    [[nodiscard]] const std::optional<NativePath>& tarball_root() const noexcept { return tarball_root_; }

private:
    XdgPaths() = default;

    NativePath home_;
    NativePath config_home_;
    NativePath data_home_;
    NativePath cache_home_;
    NativePath state_home_;
    std::optional<NativePath> runtime_dir_;
    NativePath exe_dir_;
    u32 uid_ = 0;
    std::string user_name_;
    std::optional<NativePath> appimage_;
    std::optional<NativePath> tarball_root_;
};

}  // namespace reboot::os_linux::platform
