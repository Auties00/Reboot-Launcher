#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/support/support_cell.hpp"
#include "reboot/support/support_provider.hpp"
#include "reboot/support/support_query.hpp"
#include "reboot/support/support_reason.hpp"
#include "reboot/support/support_tier.hpp"

namespace rb::support {

// The cell a query fell in, with the query's own reasons applied on top of it.
struct SupportVerdict {
    SupportTier tier = SupportTier::Untested;
    SupportProvider provider = SupportProvider::Ours;
    // Empty only when Tested. Blocked reasons come first, so the first entry set the tier.
    std::vector<SupportReason> reasons;
    // Absent when the version is unknown or no cell contains the build.
    std::optional<SupportCell> cell;
    // Above the cap for an imported build that has no opt-in yet.
    bool opt_in_available = false;

    bool operator==(const SupportVerdict&) const = default;
};

// Fails for a Blocked verdict with the diagnostic of its first reason; the other Blocked reasons
// are its causes. Untested passes; the caller raises ConfirmUntested.
[[nodiscard]] Result<void> check_not_blocked(const SupportQuery& query, const SupportVerdict& verdict);

}  // namespace rb::support
