#include "reboot/integration/shell_error.hpp"

#include <utility>

#include "messages.hpp"

namespace reboot::integration {

namespace {

[[nodiscard]] Diagnostic finish(const ShellError& error, DiagBuilder&& builder, ErrorKind kind) {
    if (error.cause) std::move(builder).cause(*error.cause);
    return std::move(builder).kind(kind).build();
}

// The link or the path the action was given.
[[nodiscard]] DiagBuilder with_target(const ShellError& error, MessageId message) {
    DiagBuilder builder = make_diag(ErrorDomain::Integration, message);
    if (error.action == ShellAction::OpenUrl) return std::move(builder).arg("target", error.url);
    return std::move(builder).arg("target", error.path.value_or(NativePath{}));
}

}  // namespace

Diagnostic to_diagnostic(const ShellError& error) {
    const auto diag = [](MessageId message) { return make_diag(ErrorDomain::Integration, message); };
    switch (error.code) {
        case ShellErrorCode::EngineInOtherSession:
            return finish(error,
                          diag(msg::kEngineInOtherSession)
                              .arg("engine_session", error.engine_session)
                              .arg("caller_session", error.caller_session),
                          ErrorKind::Conflict);
        case ShellErrorCode::NoDisplay: return finish(error, diag(msg::kNoDisplay), ErrorKind::Unsupported);
        case ShellErrorCode::NotHttps: return finish(error, diag(msg::kUrlNotHttps), ErrorKind::InvalidInput);
        case ShellErrorCode::NotAbsolute:
            return finish(error, diag(msg::kPathNotAbsolute).arg("path", error.path.value_or(NativePath{})),
                          ErrorKind::InvalidInput);
        case ShellErrorCode::ShellFailed:
            return finish(error, with_target(error, msg::kShellFailed), ErrorKind::Generic);
        case ShellErrorCode::Cancelled:
            return finish(error, with_target(error, msg::kShellCancelled), ErrorKind::Cancelled);
    }
    return internal_bug("integration::to_diagnostic(ShellError)");
}

}  // namespace reboot::integration
