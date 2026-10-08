#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/storage/settings_values.hpp"
#include "reboot/updates/activity_probe.hpp"
#include "reboot/updates/update_offer.hpp"

namespace reboot::updates {

// WaitingForIdle: staged behind the ApplyGate. Draining: a consented Drain{Update} is ending live work.
enum class UpdatePhase : u8 {
    Idle,
    Checking,
    Available,
    Downloading,
    Staged,
    WaitingForIdle,
    Draining,
    Applying,
    Failed,
};

// NotifyOnly where the package manager or container owns the install (IUpdateApplier::supports_in_place).
enum class UpdateMode : u8 { InPlace, NotifyOnly };

// Now drains live sessions after ConfirmStopSessions; WhenIdle waits for the gate.
enum class ApplyWhen : u8 { WhenIdle, Now };

// What the apply op completes with once the apply is armed; the restart itself is service state.
enum class ApplyStatus : u8 { Applying, WaitingForIdle, Draining };

enum class CheckTrigger : u8 { Startup, Periodic, User };

struct UpdateState {
    SemVer installed;
    UpdateMode mode = UpdateMode::InPlace;
    storage::UpdateChannel channel = storage::UpdateChannel::Stable;
    UpdatePhase phase = UpdatePhase::Idle;
    std::optional<UpdateOffer> offer;
    // Persisted, so it survives engine restarts.
    std::optional<std::chrono::system_clock::time_point> last_check;
    std::optional<Diagnostic> last_error;
    // Set in Staged, WaitingForIdle and Draining.
    std::vector<LiveActivity> blocking;
};

}  // namespace reboot::updates
