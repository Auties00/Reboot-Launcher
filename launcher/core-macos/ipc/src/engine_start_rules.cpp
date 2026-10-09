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

AgentStep agent_step(AgentStatus status) {
    switch (status) {
        case AgentStatus::Enabled: return AgentStep::Kickstart;
        case AgentStatus::RequiresApproval: return AgentStep::AwaitingUser;
        case AgentStatus::NotRegistered:
        case AgentStatus::NotFound: return AgentStep::Register;
    }
    return AgentStep::Register;
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
