#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::os_windows::ipc {

// Covers no capability ids; IPlatformPaths for reboot_client, which may not link
// core-windows/platform. The install layout comes from foundation's velopack_root_of, which
// os_windows::platform::WindowsPaths calls too.
class WindowsClientPaths final : public ports::IPlatformPaths {
public:
    // A failing SHGetKnownFolderPath or GetModuleHandleExW is platform.ipc_call_failed.
    [[nodiscard]] static Result<WindowsClientPaths> detect();

    // %LOCALAPPDATA%\Reboot Launcher.
    [[nodiscard]] NativePath default_data_root() const override;
    // <data root>\cache.
    [[nodiscard]] NativePath default_cache_root() const override;
    // <data root>\logs.
    [[nodiscard]] NativePath default_logs_root() const override;
    // Always empty: the engine endpoint is a named pipe.
    [[nodiscard]] NativePath ipc_runtime_base() const override;
    // reboot_client.dll's directory, where reboot-engine.exe ships, not the host exe's.
    [[nodiscard]] NativePath exe_dir() const override;
    // Velopack when velopack_root_of(exe_dir) finds the layout, Portable otherwise.
    [[nodiscard]] ports::InstallKind install_kind() const override;
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override;

private:
    WindowsClientPaths(NativePath local_app_data, NativePath exe_dir, std::optional<NativePath> velopack_root);

    NativePath local_app_data_;
    NativePath exe_dir_;
    std::optional<NativePath> velopack_root_;
};

}  // namespace reboot::os_windows::ipc
