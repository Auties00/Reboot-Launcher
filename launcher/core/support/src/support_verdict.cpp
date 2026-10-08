#include "reboot/support/support_verdict.hpp"

#include <cstddef>
#include <expected>
#include <utility>

namespace reboot::support {

Result<void> check_not_blocked(const SupportQuery& query, const SupportVerdict& verdict) {
    if (verdict.tier != SupportTier::Blocked) return {};
    if (verdict.reasons.empty()) return std::unexpected(internal_bug("support::check_not_blocked"));
    Diagnostic first = to_diagnostic(verdict.reasons.front(), query);
    for (std::size_t i = 1; i < verdict.reasons.size(); ++i)
        if (reason_tier(verdict.reasons[i]) == SupportTier::Blocked)
            first.causes.push_back(to_diagnostic(verdict.reasons[i], query));
    return std::unexpected(std::move(first));
}

}  // namespace reboot::support
