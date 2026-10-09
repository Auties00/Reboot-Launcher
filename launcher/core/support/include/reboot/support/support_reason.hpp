#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/support/support_query.hpp"
#include "reboot/support/support_tier.hpp"

namespace rb::support {

enum class SupportReason : u8 {
    // Blocked: no confirmed game version yet; the engine asks with ChooseVersion.
    VersionUnknown,
    // Blocked: above kMaxSupportedVersion, with no opt-in or for a build that is not imported.
    AboveVersionCap,
    // Untested: above kMaxSupportedVersion, used through the per-build opt-in.
    AboveVersionCapOptedIn,
    // Blocked: the runner has no pinned runtime on this OS, or a host query names a Wine runner.
    RunnerUnavailable,
    // Blocked: the game-server binary has not been described.
    GameServerUnavailable,
    // Blocked: no range of the game-server description covers the version and changelist.
    NotCoveredByGameServer,
    // Untested: the user's auth DLL replaces ours (legacy fixed-origin mode).
    CustomAuthDll,
    // Untested: a Local or Remote backend has no content revision we can key evidence to.
    ExternalBackend,
    // Untested: no evidence record exists for this cell.
    NoEvidence,
    // Untested: evidence exists, but only for inputs that differ from the current ones.
    EvidenceStale,
    // Untested: the newest record for the current inputs is a failed run.
    EvidenceFailed,
};

[[nodiscard]] constexpr SupportTier reason_tier(SupportReason reason) noexcept {
    switch (reason) {
        case SupportReason::VersionUnknown:
        case SupportReason::AboveVersionCap:
        case SupportReason::RunnerUnavailable:
        case SupportReason::GameServerUnavailable:
        case SupportReason::NotCoveredByGameServer: return SupportTier::Blocked;
        case SupportReason::AboveVersionCapOptedIn:
        case SupportReason::CustomAuthDll:
        case SupportReason::ExternalBackend:
        case SupportReason::NoEvidence:
        case SupportReason::EvidenceStale:
        case SupportReason::EvidenceFailed: return SupportTier::Untested;
    }
    return SupportTier::Blocked;
}

// The query supplies the version, role and opt-in state the message names. Blocked reasons are
// Errors of kind Unsupported; Untested ones are Warnings.
[[nodiscard]] Diagnostic to_diagnostic(SupportReason reason, const SupportQuery& query);

}  // namespace rb::support
