#pragma once

#include <chrono>

namespace rb::testing {

// Steps shared by the game-control peer scripts, besides the contract events they send.

// Waits on the peer's clock before the next step.
struct ScriptPause {
    std::chrono::milliseconds duration{0};
};

// Closes the connection, as a crashed game or winhost would.
struct ScriptDisconnect {};

// Stops answering Ping while staying connected, so the engine sees a hang.
struct ScriptStopPonging {};

}  // namespace rb::testing
