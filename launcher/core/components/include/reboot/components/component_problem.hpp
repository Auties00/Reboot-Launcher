#pragma once

#include <optional>
#include <vector>

#include "reboot/components/component_ref.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::components {

// - VanishedAfterDownload: a downloaded file in .staging vanished or became unreadable before its
//   hash, the usual Defender quarantine on write-close.
// - FileVanishedAfterVerify: a stored file was found missing, unreadable or changed on re-check.
// - HelperQuarantined: either of the above on reboot-winhost.exe, which every Wine play session
//   needs.
enum class ComponentProblemKind : u8 { VanishedAfterDownload, FileVanishedAfterVerify, HelperQuarantined };

// What the UI offers. CopyRestoreCommand is a command the UI builds from ComponentProblem::file.
// No step adds a security-product exclusion: os-integration-ledger 7 drops the automatic one,
// and the consented data-dir exclusion of dll-toolchain-av 5 waits on the owner's signing-identity
// question.
enum class RemediationStep : u8 {
    RestoreFromQuarantine,
    OpenSecurityCenter,
    CopyRestoreCommand,
    ReportFalsePositive,
    ExplainSmartAppControl,
};

enum class RecoveryState : u8 { Refetching, Recovered, RefetchFailed };

// `security` is set only for a missing or access-denied file, and stays nullopt where the OS has
// no security center (macOS, Linux) or the probe failed.
struct ComponentProblem {
    ComponentProblemKind kind{};
    ComponentRef component;
    NativePath file;
    std::optional<ports::SecurityProducts> security;
    std::vector<RemediationStep> remediation;
    RecoveryState recovery = RecoveryState::Refetching;
    std::optional<Diagnostic> refetch_error;
};

// Capabilities: os-integration.antivirus-detection.
[[nodiscard]] std::vector<RemediationStep> guided_remediation(const std::optional<ports::SecurityProducts>& security);

// Names security software only when `security` lists a product: components.download_quarantined,
// file_quarantined or helper_quarantined, with the product names and the Smart App Control state
// as args; otherwise the unattributed components.download_vanished, file_vanished or
// helper_vanished.
[[nodiscard]] Diagnostic to_diagnostic(const ComponentProblem& problem);

}  // namespace rb::components
