#pragma once

#include <chrono>
#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

// The decisions of SmAppServiceEngineStarter, apart from the calls that feed them.
namespace reboot::os_macos::ipc {

// SMAppServiceStatus.
enum class AgentStatus : u8 { NotRegistered, Enabled, RequiresApproval, NotFound };

enum class AgentStep : u8 { Kickstart, Register, AwaitingUser };

// launchctl's exit status for a service its domain does not know.
inline constexpr int kLaunchctlServiceNotFound = 113;
// launchctl's exit status for a service the user switched off in Login Items.
inline constexpr int kLaunchctlServiceDisabled = 119;

struct LaunchctlRun {
    enum class End : u8 { Exited, Signalled, TimedOut };

    End end = End::Exited;
    // The exit status for Exited, the signal for Signalled.
    int code = 0;
};

// ElevatedRefused, NoInteractiveSession, then CannotDetach for an overridden root; nullopt lets
// the start go on.
[[nodiscard]] std::optional<ports::StartResult> refusal_before_start(const ports::CallerContext& caller,
                                                                     const DataRoot& root);

// NotFound is registered too: SMAppService reports it for an agent that was never registered.
[[nodiscard]] AgentStep agent_step(AgentStatus status);

// Started for exit 0, AwaitingUser for a label the domain does not know or has disabled, otherwise
// platform.agent_kickstart_timed_out (retryable) or platform.agent_kickstart_failed.
[[nodiscard]] Result<ports::StartResult> kickstart_outcome(const LaunchctlRun& run, std::string_view label,
                                                           std::chrono::milliseconds deadline);

// platform.agent_register_failed, with the NSError code when there is one.
[[nodiscard]] Diagnostic agent_register_failed(std::string_view label, std::optional<i64> error_code);
// platform.agent_register_timed_out, retryable.
[[nodiscard]] Diagnostic agent_register_timed_out(std::string_view label, std::chrono::milliseconds deadline);

}  // namespace reboot::os_macos::ipc
