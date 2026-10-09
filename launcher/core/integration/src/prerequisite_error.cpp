#include "reboot/integration/prerequisite_error.hpp"

#include <string>
#include <utility>

#include "messages.hpp"

namespace rb::integration {

Diagnostic to_diagnostic(const PrerequisiteError& error) {
    const std::string id =
        error.code == PrerequisiteErrorCode::UnknownId ? error.text : std::string(to_string(error.id));
    const auto finish = [&](MessageId message, ErrorKind kind) {
        DiagBuilder builder = make_diag(ErrorDomain::Integration, message).arg("id", id).kind(kind);
        if (error.cause) std::move(builder).cause(*error.cause);
        return std::move(builder).build();
    };
    switch (error.code) {
        case PrerequisiteErrorCode::UnknownId: return finish(msg::kUnknownPrerequisite, ErrorKind::NotFound);
        case PrerequisiteErrorCode::NotRemediable:
            return finish(msg::kPrerequisiteNotRemediable, ErrorKind::Unsupported);
        case PrerequisiteErrorCode::NotApplicable:
            return finish(msg::kPrerequisiteNotApplicable, ErrorKind::Unsupported);
        case PrerequisiteErrorCode::RemediationFailed: return finish(msg::kRemediationFailed, ErrorKind::Generic);
        case PrerequisiteErrorCode::StillMissing: return finish(msg::kPrerequisiteStillMissing, ErrorKind::Generic);
    }
    return internal_bug("integration::to_diagnostic(PrerequisiteError)");
}

}  // namespace rb::integration
