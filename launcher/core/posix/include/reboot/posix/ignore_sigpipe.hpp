#pragma once

#include "reboot/foundation/diag.hpp"

namespace rb::posix {

// Covers no capability ids. The engine calls it early in main, so a write to a closed pipe or
// socket fails with EPIPE instead of ending the process; SIGINT and SIGTERM stay with EngineHost's
// I/O thread. reboot_client never calls it: SIGPIPE belongs to the host app.
[[nodiscard]] Result<void> ignore_sigpipe();

}  // namespace rb::posix
