#pragma once

#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/game_lifecycle_event.hpp"
#include "reboot/injection/net_mode.hpp"
#include "reboot/sessions/session_exit.hpp"
#include "reboot/sessions/session_phase.hpp"

namespace rb::play {

// Launching runs until every planned DLL is injected, before or after resume as the boot strategy
// says. Loading waits for our DLL's Loaded; Running starts at LoggedIn.
enum class PlayPhase : u8 { Preparing, Launching, Loading, Loaded, RedirectReady, Running };

// Loading, Loaded and RedirectReady are all the registry's Loading.
[[nodiscard]] constexpr sessions::SessionPhase session_phase(PlayPhase phase) noexcept {
    switch (phase) {
        case PlayPhase::Preparing: return sessions::SessionPhase::Preparing;
        case PlayPhase::Launching: return sessions::SessionPhase::Launching;
        case PlayPhase::Loading:
        case PlayPhase::Loaded:
        case PlayPhase::RedirectReady: return sessions::SessionPhase::Loading;
        case PlayPhase::Running: return sessions::SessionPhase::Running;
    }
    return sessions::SessionPhase::Preparing;
}

// Covers game-launch.orchestration.
// What play adds to the registry's SessionInfo for one session.
struct PlaySessionState {
    SessionId session;
    PlayPhase phase = PlayPhase::Preparing;
    injection::NetMode net_mode = injection::NetMode::Isolated;
    // Ids of the optional patches and hooks that failed.
    std::vector<std::string> degraded;
    bool console_ready = false;
    // Between TravelStarted and TravelEnded, when an unresponsive game is not reported.
    bool traveling = false;
    // Set once an end was reported or a stop began; later events change nothing.
    bool ending = false;
};

// What one event asks of the driver.
struct PlayEventEffect {
    // play.features_degraded naming every id in `degraded`, raised again on each change.
    std::optional<Diagnostic> degraded;
    // For SessionRegistry::report_exit.
    std::optional<sessions::SessionExit> exit;
};

// Pure, so the state machine is tested without a game. Our DLL drives the phases in every net
// mode; a SessionFatal from LegacyOutputAdapter's markers only ends the session.
// - ExitRequested before Running is ignored, since our DLL suppressed it; from Running it is Exited.
// - A required HookFailed is Fatal (play.hook_failed), as is a DllStep fatal (play.fatal).
// - CorruptBuild is Fatal (play.corrupt_build) before Running and Crashed (play.crashed) after it.
[[nodiscard]] PlayEventEffect apply(PlaySessionState& state, const game_channel::GameLifecycleEvent& event);

// The game process exited on its own: before Running, Exited with play.exited_before_login; after
// it, Crashed with play.crashed for an NTSTATUS error code (0xC0000000 and up), else Exited.
[[nodiscard]] sessions::SessionExit exit_for_game_exit(const PlaySessionState& state, std::optional<i32> exit_code);

}  // namespace rb::play
