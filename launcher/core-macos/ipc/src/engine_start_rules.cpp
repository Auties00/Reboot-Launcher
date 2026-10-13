#include "engine_start_rules.hpp"

#include <string>

#include "messages.hpp"

namespace rb::os_macos::ipc {

std::optional<ports::StartResult> refusal_before_start(const ports::CallerContext& caller, const DataRoot& root) {
    if (caller.elevated) return ports::StartResult::ElevatedRefused;
    if (!caller.interactive) return ports::StartResult::NoInteractiveSession;
    // The one agent serves the default root only.
    if (root.overridden) return ports::StartResult::CannotDetach;
    return std::nullopt;
}

AgentStep agent_step(AgentStatus status, bool translocated) {
    switch (status) {
        case AgentStatus::Enabled: return AgentStep::Kickstart;
        case AgentStatus::RequiresApproval: return AgentStep::AwaitingUser;
        case AgentStatus::NotRegistered:
        case AgentStatus::NotFound: break;
    }
    return translocated ? AgentStep::Translocated : AgentStep::Register;
}

bool kickstart_retry_after_register(const LaunchctlRun& run, bool registered_now) {
    return registered_now && run.end == LaunchctlRun::End::Exited && run.code == kLaunchctlServiceNotFound;
}

Result<LaunchctlRun> kickstart_with_retries(KickstartCalls& calls, bool registered_now, std::chrono::milliseconds budget,
                                            std::chrono::milliseconds interval) {
    using std::chrono::milliseconds;
    const auto until = calls.now() + budget;
    milliseconds deadline = budget;
    for (;;) {
        Result<LaunchctlRun> run = calls.run(deadline);
        if (!run || !kickstart_retry_after_register(*run, registered_now)) return run;
        if (until - calls.now() < interval + kKickstartMinTry) return run;
        calls.pause(interval);
        // Measured again: the pause may have overshot.
        const auto left = std::chrono::duration_cast<milliseconds>(until - calls.now());
        if (left < kKickstartMinTry) return run;
        deadline = left;
    }
}

Result<ports::StartResult> kickstart_outcome(const LaunchctlRun& run, std::string_view label,
                                             std::chrono::milliseconds deadline) {
    switch (run.end) {
        case LaunchctlRun::End::Exited:
            if (run.code == 0) return ports::StartResult::Started;
            if (run.code == kLaunchctlServiceNotFound || run.code == kLaunchctlServiceDisabled)
                return ports::StartResult::AwaitingUser;
            return make_diag(ErrorDomain::Platform, kAgentKickstartFailed)
                .arg("label", label)
                .detail("exit status " + std::to_string(run.code))
                .fail();
        case LaunchctlRun::End::Signalled:
            return make_diag(ErrorDomain::Platform, kAgentKickstartFailed)
                .arg("label", label)
                .detail("signal " + std::to_string(run.code))
                .fail();
        case LaunchctlRun::End::TimedOut: break;
    }
    return make_diag(ErrorDomain::Platform, kAgentKickstartTimedOut)
        .arg("label", label)
        .arg("deadline", deadline)
        .retryable()
        .fail();
}

Diagnostic app_translocated() {
    return make_diag(ErrorDomain::Platform, kAppTranslocated).kind(ErrorKind::Conflict);
}

Diagnostic agent_register_failed(std::string_view label, std::optional<i64> error_code) {
    DiagBuilder diag = make_diag(ErrorDomain::Platform, kAgentRegisterFailed).arg("label", label);
    if (error_code) return std::move(diag).os(SystemError{SystemError::Origin::Host, *error_code});
    return diag;
}

Diagnostic agent_register_timed_out(std::string_view label, std::chrono::milliseconds deadline) {
    return make_diag(ErrorDomain::Platform, kAgentRegisterTimedOut)
        .arg("label", label)
        .arg("deadline", deadline)
        .retryable();
}

}  // namespace rb::os_macos::ipc
