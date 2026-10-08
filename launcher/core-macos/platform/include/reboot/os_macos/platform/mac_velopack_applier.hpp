#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IUpdateApplier for the app bundle; Velopack's restart would open the GUI.
class MacVelopackApplier final : public ports::IUpdateApplier {
public:
    // UpdateMac is killed past this, so it can never swap the bundle under a still-running old engine.
    inline static constexpr std::chrono::seconds kSwapDeadline{120};

    // `launchd_label` is XPC_SERVICE_NAME, nullopt for a --foreground engine.
    MacVelopackApplier(std::optional<NativePath> app_bundle, NativePath feed_dir,
                       std::optional<std::string> launchd_label);

    Result<void> stage(const NativePath& package) override;
    // Blocks up to kSwapDeadline, so it runs on a WorkerPool worker, never on the strand.
    // Waits for UpdateMac, our own child, to exit before reading the version from Info.plist.
    // A failed execve falls back to `launchctl kickstart -k`, since KeepAlive{Crashed} ignores exits.
    Result<void> apply_and_restart(std::vector<std::string> args) override;
    // True for an installed, untranslocated bundle.
    [[nodiscard]] bool supports_in_place() const override { return app_bundle_.has_value(); }

private:
    std::optional<NativePath> app_bundle_;
    NativePath feed_dir_;
    std::optional<std::string> launchd_label_;
    std::optional<SemVer> staged_version_;
};

}  // namespace reboot::os_macos::platform
