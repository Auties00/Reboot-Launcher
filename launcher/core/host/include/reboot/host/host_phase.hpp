#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/types.hpp"
#include "reboot/sessions/session_phase.hpp"

namespace rb::host {

// Spawning covers the hello and welcome; a respawn after match end or ListenFailed goes
// through Spawning again while the session itself stays Running. LiveUnpublished is a server
// that is up but missed the readiness deadline, or a second auto-server sharing the auto profile.
enum class HostPhase : u8 {
    Preparing,
    Spawning,
    WaitingForListen,
    WaitingForReadiness,
    MappingPorts,
    Publishing,
    Live,
    LiveUnpublished,
    Restarting,
    Draining,
    Stopping,
    Stopped,
    Failed,
};

// The registry phase each host phase reports; nullopt for the ones only the registry's stop
// path enters.
[[nodiscard]] constexpr std::optional<sessions::SessionPhase> session_phase_for(HostPhase phase) noexcept {
    switch (phase) {
        case HostPhase::Preparing: return sessions::SessionPhase::Preparing;
        case HostPhase::Spawning:
        case HostPhase::WaitingForListen: return sessions::SessionPhase::Launching;
        case HostPhase::WaitingForReadiness:
        case HostPhase::MappingPorts:
        case HostPhase::Publishing: return sessions::SessionPhase::Loading;
        case HostPhase::Live:
        case HostPhase::LiveUnpublished:
        case HostPhase::Restarting:
        case HostPhase::Draining: return sessions::SessionPhase::Running;
        case HostPhase::Stopping:
        case HostPhase::Stopped:
        case HostPhase::Failed: return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view host_phase_name(HostPhase phase) noexcept {
    switch (phase) {
        case HostPhase::Preparing: return "preparing";
        case HostPhase::Spawning: return "spawning";
        case HostPhase::WaitingForListen: return "waiting_for_listen";
        case HostPhase::WaitingForReadiness: return "waiting_for_readiness";
        case HostPhase::MappingPorts: return "mapping_ports";
        case HostPhase::Publishing: return "publishing";
        case HostPhase::Live: return "live";
        case HostPhase::LiveUnpublished: return "live_unpublished";
        case HostPhase::Restarting: return "restarting";
        case HostPhase::Draining: return "draining";
        case HostPhase::Stopping: return "stopping";
        case HostPhase::Stopped: return "stopped";
        case HostPhase::Failed: return "failed";
    }
    return "unknown";
}

}  // namespace rb::host
