#include "reboot/integration/purge_error.hpp"

#include <utility>

#include "messages.hpp"

namespace reboot::integration {

namespace {

[[nodiscard]] Diagnostic finish(const PurgeError& error, DiagBuilder&& builder) {
    for (const Diagnostic& cause : error.causes) std::move(builder).cause(cause);
    return std::move(builder).build();
}

}  // namespace

Diagnostic to_diagnostic(const PurgeError& error) {
    const NativePath path = error.path.value_or(NativePath{});
    switch (error.code) {
        case PurgeErrorCode::Blocked:
            return finish(error, make_diag(ErrorDomain::Integration, msg::kPurgeBlocked)
                                     .arg("sessions", error.sessions)
                                     .arg("operations", error.ops)
                                     .kind(ErrorKind::Conflict)
                                     .retryable());
        case PurgeErrorCode::UnsafeTarget:
            return finish(error, make_diag(ErrorDomain::Integration, msg::kPurgeUnsafeTarget).arg("path", path));
        case PurgeErrorCode::RemoveFailed:
            return finish(error,
                          make_diag(ErrorDomain::Integration, msg::kPurgeFailed).arg("path", path).retryable());
    }
    return internal_bug("integration::to_diagnostic(PurgeError)");
}

}  // namespace reboot::integration
