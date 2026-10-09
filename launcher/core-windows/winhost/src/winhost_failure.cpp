#include "reboot/os_windows/winhost/winhost_failure.hpp"

#include <string>
#include <utility>

#include "reboot/foundation/diag.hpp"

namespace rb::os_windows::winhost {

namespace {

// Registered by core/game_channel; winhost names it without linking that package.
constexpr MessageId kRequestFailed{"game_channel.request_failed"};

}  // namespace

std::string_view step_name(FailureStep step) noexcept {
    switch (step) {
        case FailureStep::Bootstrap: return "bootstrap";
        case FailureStep::Connect: return "connect";
        case FailureStep::Handshake: return "handshake";
        case FailureStep::Protocol: return "protocol";
        case FailureStep::Spawn: return "spawn";
        case FailureStep::Inject: return "inject";
        case FailureStep::Resume: return "resume";
        case FailureStep::Stop: return "stop";
        case FailureStep::Relay: return "relay";
    }
    return "unknown";
}

contracts::winhost::WhFatal to_fatal(const WinhostFailure& failure) {
    return contracts::winhost::WhFatal{std::string(step_name(failure.step)), failure.os_code};
}

contracts::common::CommandResult failed_reply(u64 req_id, std::string_view request, const WinhostFailure& failure) {
    Diagnostic diag = make_diag(ErrorDomain::GameChannel, kRequestFailed)
                          .arg("role", "winhost")
                          .arg("request", request)
                          .detail(std::string(step_name(failure.step)))
                          .build();
    if (failure.os_code) diag.os_error = SystemError{SystemError::Origin::GuestWindows, *failure.os_code};
    return contracts::common::CommandResult{req_id, false, contracts::common::to_wire(diag)};
}

}  // namespace rb::os_windows::winhost
