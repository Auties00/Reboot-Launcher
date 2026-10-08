#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::gameserver {

// Failed covers the server's own CommandResult{ok = false}, no reply in time and a server that
// exited first.
enum class CommandStatus : u8 { Ok, Failed, Unsupported };

// The one answer to one OperatorCommand; `error` is set exactly when Failed.
struct CommandResult {
    CommandStatus status = CommandStatus::Ok;
    std::optional<Diagnostic> error;
};

}  // namespace reboot::gameserver
