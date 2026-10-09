#include "reboot/support/support_reason.hpp"

#include <string>
#include <utility>

#include "messages.hpp"
#include "reboot/support/version_cap.hpp"

namespace rb::support {

Diagnostic to_diagnostic(SupportReason reason, const SupportQuery& query) {
    const std::string version = query.version ? query.version->canonical() : std::string();
    const std::string cap = kMaxSupportedVersion.canonical();
    const auto finish = [reason](DiagBuilder&& builder) {
        if (reason_tier(reason) == SupportTier::Blocked) return std::move(builder).kind(ErrorKind::Unsupported).build();
        return std::move(builder).severity(Severity::Warning).build();
    };
    switch (reason) {
        case SupportReason::VersionUnknown: return finish(make_diag(ErrorDomain::Support, msg::kVersionUnknown));
        case SupportReason::AboveVersionCap:
            return finish(make_diag(ErrorDomain::Support,
                                    query.imported ? msg::kAboveVersionCapOptInRequired : msg::kAboveVersionCap)
                              .arg("version", version)
                              .arg("cap", cap));
        case SupportReason::AboveVersionCapOptedIn:
            return finish(
                make_diag(ErrorDomain::Support, msg::kAboveVersionCapOptedIn).arg("version", version).arg("cap", cap));
        case SupportReason::RunnerUnavailable:
            return finish(make_diag(ErrorDomain::Support, query.role == SupportRole::Host ? msg::kHostNeedsNativeRunner
                                                                                          : msg::kRunnerUnavailable));
        case SupportReason::GameServerUnavailable:
            return finish(make_diag(ErrorDomain::Support, msg::kGameServerUnavailable));
        case SupportReason::NotCoveredByGameServer:
            return finish(make_diag(ErrorDomain::Support, msg::kNotCoveredByGameServer).arg("version", version));
        case SupportReason::CustomAuthDll: return finish(make_diag(ErrorDomain::Support, msg::kCustomAuthDll));
        case SupportReason::ExternalBackend: return finish(make_diag(ErrorDomain::Support, msg::kExternalBackend));
        case SupportReason::NoEvidence:
            return finish(make_diag(ErrorDomain::Support, msg::kNoEvidence).arg("version", version));
        case SupportReason::EvidenceStale:
            return finish(make_diag(ErrorDomain::Support, msg::kEvidenceStale).arg("version", version));
        case SupportReason::EvidenceFailed:
            return finish(make_diag(ErrorDomain::Support, msg::kEvidenceFailed).arg("version", version));
    }
    return internal_bug("support::to_diagnostic");
}

}  // namespace rb::support
