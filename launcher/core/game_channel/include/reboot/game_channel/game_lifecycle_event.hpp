#pragma once

#include <string>
#include <variant>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::game_channel {

using Loaded = contracts::game_client::Loaded;
using PatchResult = contracts::game_client::PatchResult;
using RedirectReady = contracts::game_client::RedirectReady;
using HookFailed = contracts::game_client::HookFailed;
using LoggedIn = contracts::game_client::LoggedIn;
using WindowCreated = contracts::game_client::WindowCreated;
// From a legacy session, the game's own shutdown line: kind RequestExit, code 0, no phase.
using ExitRequested = contracts::game_client::ExitRequested;
using ConsoleReady = contracts::game_client::ConsoleReady;

// Map travel began; hangs are not reported until TravelEnded.
struct TravelStarted {};
struct TravelEnded {};

// The game entered the match at `address`, as test_join asked.
struct Joined {
    std::string address;
};

// The match connection dropped; `reason` is the game's own network failure text.
struct Disconnected {
    std::string reason;
};

// DllStep comes from our DLL with its `step`; the others are LegacyOutputAdapter verdicts.
enum class FatalCause : u8 { DllStep, CorruptBuild, AuthFailure, CannotConnect };

struct SessionFatal {
    FatalCause cause{};
    // DllStep only.
    std::string step;
};

// Covers no capability ids by itself; LegacyOutputAdapter emits it for game-launch.output-monitoring.
// What the session state machine consumes, whether it came from our DLL or from output markers.
using GameLifecycleEvent = std::variant<Loaded, PatchResult, RedirectReady, HookFailed, LoggedIn, WindowCreated,
                                        ExitRequested, ConsoleReady, TravelStarted, TravelEnded, Joined,
                                        Disconnected, SessionFatal>;

}  // namespace reboot::game_channel
