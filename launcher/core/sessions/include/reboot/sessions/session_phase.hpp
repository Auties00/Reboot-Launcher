#pragma once

#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::sessions {

// Play and host move between Preparing and Running in any order; only the stop path goes further.
enum class SessionPhase : u8 { Preparing, Launching, Loading, Running, Stopping, Ended };

// Stopping is live: the session's processes may still exist.
[[nodiscard]] constexpr bool is_live(SessionPhase phase) noexcept { return phase != SessionPhase::Ended; }

// Not yet stopping: only such a session takes a phase change, a respawn or a new child.
[[nodiscard]] constexpr bool is_active(SessionPhase phase) noexcept { return phase < SessionPhase::Stopping; }

[[nodiscard]] constexpr std::string_view session_phase_name(SessionPhase phase) noexcept {
    switch (phase) {
        case SessionPhase::Preparing: return "preparing";
        case SessionPhase::Launching: return "launching";
        case SessionPhase::Loading: return "loading";
        case SessionPhase::Running: return "running";
        case SessionPhase::Stopping: return "stopping";
        case SessionPhase::Ended: return "ended";
    }
    return "unknown";
}

}  // namespace rb::sessions
