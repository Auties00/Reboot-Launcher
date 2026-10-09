#pragma once

#include <optional>
#include <span>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace reboot::os_macos::platform {

// The values of SecTrustSettingsResult.
enum class TrustResult : i32 { Invalid = 0, TrustRoot = 1, TrustAsRoot = 2, Deny = 3, Unspecified = 4 };

// One dictionary of a certificate's trust settings, reduced to what the SSL export needs.
struct TrustSettingsEntry {
    // No kSecTrustSettingsPolicy, or the Apple SSL policy.
    bool applies_to_ssl = true;
    // kSecTrustSettingsPolicyString or kSecTrustSettingsApplication narrow the entry to a host or app.
    bool constrained = false;
    // kSecTrustSettingsResult; absent means TrustRoot.
    std::optional<i32> result;
};

enum class TrustVerdict : u8 { Unspecified, Trusted, Denied };

// The first entry that applies to unconstrained SSL and decides; an empty list trusts as root.
[[nodiscard]] TrustVerdict classify_trust_settings(std::span<const TrustSettingsEntry> entries);

struct CertificateVerdict {
    std::vector<u8> der;
    TrustVerdict verdict = TrustVerdict::Unspecified;
};

// Verdicts in precedence order (system, admin, user): a later decision about the same certificate
// overrides an earlier one. Returns the trusted certificates, each once, in first-seen order.
[[nodiscard]] std::vector<std::vector<u8>> trusted_anchors(std::span<const CertificateVerdict> verdicts);

}  // namespace reboot::os_macos::platform
