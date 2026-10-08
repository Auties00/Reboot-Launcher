#include "reboot/sessions/sessions_error.hpp"

#include <string>

#include "messages.hpp"

namespace reboot::sessions {

namespace {

[[nodiscard]] std::string session_text(const SessionsError& error) {
    return error.session ? format_uuid(error.session->value) : std::string();
}

[[nodiscard]] std::string phase_text(const std::optional<SessionPhase>& phase) {
    return phase ? std::string(session_phase_name(*phase)) : std::string();
}

}  // namespace

Diagnostic to_diagnostic(const SessionsError& error) {
    const auto diag = [&](MessageId message) {
        return make_diag(ErrorDomain::Sessions, message).arg("session", session_text(error));
    };
    switch (error.code) {
        case SessionsErrorCode::NotFound: return diag(msg::kNotFound).kind(ErrorKind::NotFound).build();
        case SessionsErrorCode::Ended: return diag(msg::kEnded).kind(ErrorKind::Conflict).build();
        case SessionsErrorCode::Stopping: return diag(msg::kStopping).kind(ErrorKind::Conflict).build();
        case SessionsErrorCode::RefusingNew:
            return make_diag(ErrorDomain::Sessions, msg::kRefusingNew).kind(ErrorKind::Conflict).build();
        case SessionsErrorCode::ParentNotLive: return diag(msg::kParentNotLive).kind(ErrorKind::Conflict).build();
        case SessionsErrorCode::InvalidTransition:
            return diag(msg::kInvalidTransition)
                .arg("from", phase_text(error.from))
                .arg("to", phase_text(error.to))
                .kind(ErrorKind::Conflict)
                .build();
    }
    return internal_bug("sessions_error.to_diagnostic");
}

}  // namespace reboot::sessions
