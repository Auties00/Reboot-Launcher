#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/platform_paths.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IPlatformPaths from the user's Library and the running executable.
class MacPaths final : public ports::IPlatformPaths {
public:
    // The home directory comes from getpwuid_r, since $HOME can be overridden.
    [[nodiscard]] static Result<MacPaths> detect();

    // ~/Library/{Application Support,Caches,Logs}/Reboot Launcher, not the bundle id, so the CLI agrees.
    [[nodiscard]] NativePath default_data_root() const override;
    [[nodiscard]] NativePath default_cache_root() const override;
    [[nodiscard]] NativePath default_logs_root() const override;
    // _CS_DARWIN_USER_TEMP_DIR itself, as in MacClientPaths; never the shared /tmp.
    [[nodiscard]] NativePath ipc_runtime_base() const override;
    [[nodiscard]] NativePath exe_dir() const override;
    [[nodiscard]] ports::InstallKind install_kind() const override;
    // Nullopt when translocated, since the read-only translocation mount cannot be swapped.
    [[nodiscard]] std::optional<NativePath> velopack_package_dir() const override;

    [[nodiscard]] const std::optional<NativePath>& app_bundle() const noexcept { return app_bundle_; }
    // A translocated bundle runs from a random read-only mount, so nothing may register its path.
    [[nodiscard]] bool translocated() const noexcept { return translocated_; }

private:
    MacPaths(NativePath home, NativePath user_temp_dir, NativePath exe_dir, std::optional<NativePath> app_bundle,
             bool translocated);

    NativePath home_;
    NativePath user_temp_dir_;
    NativePath exe_dir_;
    std::optional<NativePath> app_bundle_;
    bool translocated_ = false;
};

}  // namespace reboot::os_macos::platform
