#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <variant>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/sessions/lease.hpp"
#include "reboot/sessions/session_kind.hpp"
#include "reboot/sessions/session_phase.hpp"
#include "reboot/sessions/spawned_process.hpp"
#include "reboot/sessions/stop_reason.hpp"

namespace reboot::sessions {

// EventKind::SessionStateChanged: any change to SessionInfo that the other events do not carry.
struct SessionStateChanged {
    SessionId session;
    SessionKind kind = SessionKind::Play;
    SessionPhase phase = SessionPhase::Preparing;
    Lease lease;
    std::optional<SessionId> parent;
    std::string label;
    std::chrono::system_clock::time_point started_at;
};

// EventKind::SessionSpawned.
struct SessionSpawned {
    SessionId session;
    SpawnedProcess process;
};

// EventKind::SessionDegraded: a condition raised; clearing one publishes SessionStateChanged.
struct SessionDegraded {
    SessionId session;
    Diagnostic condition;
};

// EventKind::SessionEnded: the last event of a session, published after its children's.
struct SessionEnded {
    SessionId session;
    SessionKind kind = SessionKind::Play;
    std::optional<SessionId> parent;
    StopReason reason = StopReason::User;
    std::optional<i32> exit_code;
    std::optional<Diagnostic> error;
};

using SessionEvent = std::variant<SessionStateChanged, SessionSpawned, SessionDegraded, SessionEnded>;

[[nodiscard]] constexpr EventKind event_kind(const SessionStateChanged&) noexcept {
    return EventKind::SessionStateChanged;
}
[[nodiscard]] constexpr EventKind event_kind(const SessionSpawned&) noexcept { return EventKind::SessionSpawned; }
[[nodiscard]] constexpr EventKind event_kind(const SessionDegraded&) noexcept { return EventKind::SessionDegraded; }
[[nodiscard]] constexpr EventKind event_kind(const SessionEnded&) noexcept { return EventKind::SessionEnded; }

[[nodiscard]] constexpr EventKind event_kind(const SessionEvent& event) noexcept {
    return std::visit([](const auto& payload) { return event_kind(payload); }, event);
}

}  // namespace reboot::sessions
