#pragma once

#include <string_view>

#include "reboot/foundation/types.hpp"

namespace rb::sessions {

// The first reason recorded for a session wins; a later stop never overwrites it.
enum class StopReason : u8 {
    User,
    LeaseEnded,
    ParentEnded,
    BuildRemoved,
    EngineShutdown,
    Update,
    Replaced,
    // A host's match-end policy asked for the stop.
    MatchEnded,
    // The reasons below are a session ending on its own.
    Exited,
    Crashed,
    Unresponsive,
    LaunchFailed,
    Fatal,
};

[[nodiscard]] constexpr bool was_requested(StopReason reason) noexcept { return reason <= StopReason::MatchEnded; }

[[nodiscard]] constexpr std::string_view stop_reason_name(StopReason reason) noexcept {
    switch (reason) {
        case StopReason::User: return "user";
        case StopReason::LeaseEnded: return "lease_ended";
        case StopReason::ParentEnded: return "parent_ended";
        case StopReason::BuildRemoved: return "build_removed";
        case StopReason::EngineShutdown: return "engine_shutdown";
        case StopReason::Update: return "update";
        case StopReason::Replaced: return "replaced";
        case StopReason::MatchEnded: return "match_ended";
        case StopReason::Exited: return "exited";
        case StopReason::Crashed: return "crashed";
        case StopReason::Unresponsive: return "unresponsive";
        case StopReason::LaunchFailed: return "launch_failed";
        case StopReason::Fatal: return "fatal";
    }
    return "unknown";
}

}  // namespace rb::sessions
