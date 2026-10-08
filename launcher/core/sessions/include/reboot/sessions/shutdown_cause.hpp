#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/sessions/stop_reason.hpp"

namespace reboot::sessions {

// OsSignal: SIGTERM, SIGINT, logoff or a closed console. Drain*: Engine.drain after the user consented.
enum class ShutdownCause : u8 { Requested, Idle, OsSignal, DrainUpdate, DrainUserStop, DrainReplace };

[[nodiscard]] constexpr StopReason stop_reason_for(ShutdownCause cause) noexcept {
    switch (cause) {
        case ShutdownCause::DrainUpdate: return StopReason::Update;
        case ShutdownCause::DrainReplace: return StopReason::Replaced;
        case ShutdownCause::Requested:
        case ShutdownCause::Idle:
        case ShutdownCause::OsSignal:
        case ShutdownCause::DrainUserStop: return StopReason::EngineShutdown;
    }
    return StopReason::EngineShutdown;
}

}  // namespace reboot::sessions
