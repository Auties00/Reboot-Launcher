#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace rb::testing {

// The exit code of a fake child that could not bind what Welcome named, as the contracts require.
inline constexpr int kBindFailureExitCode = 3;
// A fake executable given bad arguments or a bad script.
inline constexpr int kBadInvocationExitCode = 2;

// When a scripted failure's timer starts. Ready is the backend's Ready or the game server's Listening.
enum class ScriptStage : u8 { Start, Welcome, Ready };

struct ScriptedFailure {
    ScriptStage stage = ScriptStage::Ready;
    std::chrono::milliseconds after{0};
};

// Misbehaviour shared by FakeBackend and FakeGameServer. All defaults off: a conforming child.
struct ChildMisbehaviour {
    // Sent in Hello instead of this build's protocol, for child.protocol_mismatch.
    std::optional<u32> protocol;
    bool send_hello = true;
    std::chrono::milliseconds hello_delay{0};
    // Before every reply.
    std::chrono::milliseconds reply_delay{0};
    std::optional<ScriptedFailure> crash;
    int crash_exit_code = 70;
    // Stops sending Pong and replies while keeping the process and its pipes alive.
    std::optional<ScriptedFailure> hang;
    // Only the grace-then-kill path can then end the child.
    bool ignore_stdin_eof = false;
    // Request frame types answered with Unsupported instead of handled.
    std::vector<u64> unsupported;
    // Written to stderr at start; stderr is never fatal to the engine.
    std::vector<std::string> stderr_lines;
    // A frame of this type with an undecodable payload, sent right after Hello.
    std::optional<u64> garbage_frame;
};

}  // namespace rb::testing
