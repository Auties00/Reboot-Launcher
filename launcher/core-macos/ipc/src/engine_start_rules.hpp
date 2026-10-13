#pragma once

#include <chrono>
#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

// The decisions of SmAppServiceEngineStarter, apart from the calls that feed them.
namespace rb::os_macos::ipc {

// SMAppServiceStatus.
enum class AgentStatus : u8 { NotRegistered, Enabled, RequiresApproval, NotFound };

enum class AgentStep : u8 { Kickstart, Register, AwaitingUser, Translocated };

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
// A translocated app registers nothing, since launchd would keep a path on a random read-only
// mount that breaks once the app is moved; an agent already enabled is still kickstarted.
[[nodiscard]] AgentStep agent_step(AgentStatus status, bool translocated);

// Started for exit 0, AwaitingUser for a label the domain does not know or has disabled, otherwise
// platform.agent_kickstart_timed_out (retryable) or platform.agent_kickstart_failed.
[[nodiscard]] Result<ports::StartResult> kickstart_outcome(const LaunchctlRun& run, std::string_view label,
                                                           std::chrono::milliseconds deadline);

// True for a label the domain does not know yet right after this call registered it: launchd may
// load the job after register() returns.
[[nodiscard]] bool kickstart_retry_after_register(const LaunchctlRun& run, bool registered_now);

// A retried kickstart gets at least this, or is not made: launchctl itself needs some of it.
inline constexpr std::chrono::milliseconds kKickstartMinTry{250};

struct KickstartCalls {
    // One launchctl kickstart, SIGKILLed past `deadline`.
    UniqueFunction<Result<LaunchctlRun>(std::chrono::milliseconds deadline)> run;
    UniqueFunction<void(std::chrono::milliseconds pause)> pause;
    UniqueFunction<std::chrono::steady_clock::time_point()> now;
};

// Kickstarts within `budget` for all tries together. Only kickstart_retry_after_register retries,
// after `interval`, and only while kKickstartMinTry of the budget is left once the pause is over;
// otherwise the last try stands, so a pause that overshoots never turns 113 into a time-out.
[[nodiscard]] Result<LaunchctlRun> kickstart_with_retries(KickstartCalls& calls, bool registered_now,
                                                          std::chrono::milliseconds budget,
                                                          std::chrono::milliseconds interval);

// platform.app_translocated (Conflict): move the app to Applications and open it again.
[[nodiscard]] Diagnostic app_translocated();

// platform.agent_register_failed, with the NSError code when there is one.
[[nodiscard]] Diagnostic agent_register_failed(std::string_view label, std::optional<i64> error_code);
// platform.agent_register_timed_out, retryable.
[[nodiscard]] Diagnostic agent_register_timed_out(std::string_view label, std::chrono::milliseconds deadline);

}  // namespace rb::os_macos::ipc
