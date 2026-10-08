#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_windows::platform {

enum class VelopackHook : u8 { AfterInstall, BeforeUpdate, AfterUpdate, BeforeUninstall };

// First call in reboot-engine's main, since the engine is Velopack's mainExe. A hook run exits
// after `on_hook`, within Velopack's 30 s and without a UAC prompt.
void run_velopack_startup(UniqueFunction<void(VelopackHook)> on_hook);

// Covers no capability ids; IUpdateApplier for a Velopack per-user install.
class VelopackApplier final : public ports::IUpdateApplier {
public:
    // `velopack_root` is nullopt for a portable install, which cannot stage. `feed_dir` is the
    // owner-only local feed Velopack updates from.
    VelopackApplier(std::optional<NativePath> velopack_root, NativePath feed_dir);

    Result<void> stage(const NativePath& package) override;
    // Keeps no staged handle: a fresh UpdateManager over `feed_dir` finds the pending asset.
    Result<void> apply_and_restart(std::vector<std::string> args) override;
    // Windows cannot replace a running image, so the engine exits for Update.exe.
    [[nodiscard]] bool supports_in_place() const override { return false; }

private:
    std::optional<NativePath> velopack_root_;
    NativePath feed_dir_;
};

}  // namespace reboot::os_windows::platform
