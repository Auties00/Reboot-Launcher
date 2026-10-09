#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; IPlatformPaths from the known folders and the running exe.
class WindowsPaths final : public ports::IPlatformPaths {
public:
    [[nodiscard]] static Result<WindowsPaths> detect();

    [[nodiscard]] NativePath default_data_root() const override;
    [[nodiscard]] NativePath default_cache_root() const override;
    [[nodiscard]] NativePath default_logs_root() const override;
    // Empty: the engine endpoint is a named pipe.
    [[nodiscard]] NativePath ipc_runtime_base() const override;
    [[nodiscard]] NativePath exe_dir() const override;
    // Velopack only when exe_dir is <root>\current beside <root>\Update.exe and sq.version.
    [[nodiscard]] ports::InstallKind install_kind() const override;
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override;

private:
    WindowsPaths(NativePath local_app_data, NativePath exe_dir, std::optional<NativePath> velopack_root);

    NativePath local_app_data_;
    NativePath exe_dir_;
    std::optional<NativePath> velopack_root_;
};

}  // namespace rb::os_windows::platform
