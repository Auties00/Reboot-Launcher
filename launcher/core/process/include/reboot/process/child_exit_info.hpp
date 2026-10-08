#pragma once

#include <chrono>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"

namespace reboot::process {

// Requested is never reported as a crash, whatever the exit code.
enum class ChildExitCause : u8 { Requested, Exited, Unresponsive, HandshakeFailed, ProtocolError, SpawnFailed };

// Failed: not restarted, because there is no RestartPolicy, its limit was reached, or the
// protocol did not match (the same binary would fail the same way).
enum class AfterExit : u8 { Restarting, Stopped, Failed };

// Covers no capability ids; one per spawn attempt, delivered through ChildObserver::on_exit.
struct ChildExitInfo {
    ChildExitCause cause{};
    AfterExit after{};
    u32 generation = 0;
    // Absent for SpawnFailed.
    std::optional<u32> pid;
    ports::ChildExit status;
    // Absent only for Requested.
    std::optional<Diagnostic> error;
    // Set when `after` is Restarting.
    std::chrono::milliseconds restart_delay{};
};

}  // namespace reboot::process
