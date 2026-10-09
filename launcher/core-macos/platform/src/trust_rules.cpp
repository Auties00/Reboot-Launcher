#include "trust_rules.hpp"

#include <algorithm>
#include <cstddef>

namespace reboot::os_macos::platform {

TrustVerdict classify_trust_settings(std::span<const TrustSettingsEntry> entries) {
    if (entries.empty()) return TrustVerdict::Trusted;
    for (const TrustSettingsEntry& entry : entries) {
        if (!entry.applies_to_ssl || entry.constrained) continue;
        switch (static_cast<TrustResult>(entry.result.value_or(static_cast<i32>(TrustResult::TrustRoot)))) {
            case TrustResult::TrustRoot:
            case TrustResult::TrustAsRoot:
                return TrustVerdict::Trusted;
            case TrustResult::Deny:
                return TrustVerdict::Denied;
            case TrustResult::Invalid:
            case TrustResult::Unspecified:
                break;
        }
    }
    return TrustVerdict::Unspecified;
}

std::vector<std::vector<u8>> trusted_anchors(std::span<const CertificateVerdict> verdicts) {
    std::vector<std::vector<u8>> order;
    std::vector<TrustVerdict> final_verdicts;
    for (const CertificateVerdict& verdict : verdicts) {
        const auto known = std::ranges::find(order, verdict.der);
        if (known == order.end()) {
            order.push_back(verdict.der);
            final_verdicts.push_back(verdict.verdict);
            continue;
        }
        if (verdict.verdict == TrustVerdict::Unspecified) continue;
        final_verdicts[static_cast<std::size_t>(known - order.begin())] = verdict.verdict;
    }
    std::vector<std::vector<u8>> trusted;
    for (std::size_t i = 0; i < order.size(); ++i)
        if (final_verdicts[i] == TrustVerdict::Trusted) trusted.push_back(std::move(order[i]));
    return trusted;
}

}  // namespace reboot::os_macos::platform
