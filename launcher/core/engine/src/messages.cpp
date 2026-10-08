#include "messages.hpp"

namespace reboot::engine::msg {

REBOOT_MESSAGE(kBadCommandLine, "engine.bad_command_line", "The engine does not accept the argument {argument}");
REBOOT_MESSAGE(kLockFailed, "engine.lock_failed", "Could not take the engine lock {path}");
REBOOT_MESSAGE(kNotReplaceable, "engine.not_replaceable",
               "This engine was started {origin} and cannot be replaced by another build");
REBOOT_MESSAGE(kBusy, "engine.busy", "The engine is still running sessions or operations and cannot be replaced now");
REBOOT_MESSAGE(kShuttingDown, "engine.shutting_down", "The engine is shutting down and starts nothing new");
REBOOT_MESSAGE(kAlreadyDraining, "engine.already_draining", "The engine is already draining for {reason}");
REBOOT_MESSAGE(kExitPending, "engine.exit_pending", "The engine already waits until it is idle to {exit}");
REBOOT_MESSAGE(kAnswerMismatch, "engine.answer_mismatch",
               "The answer does not fit request {request}, which asks for {kind}");
REBOOT_MESSAGE(kConflictingPlayTarget, "engine.conflicting_play_target",
               "A play request cannot name the automatic server together with a server or address");

}  // namespace reboot::engine::msg
