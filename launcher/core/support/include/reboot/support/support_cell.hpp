#pragma once

#include <optional>
#include <vector>

#include "reboot/support/evidence_record.hpp"
#include "reboot/support/support_cell_key.hpp"
#include "reboot/support/support_provider.hpp"
#include "reboot/support/support_reason.hpp"
#include "reboot/support/support_tier.hpp"

namespace reboot::support {

// One evaluated cell of the matrix that UIs render.
struct SupportCell {
    SupportCellKey key;
    SupportTier tier = SupportTier::Untested;
    SupportProvider provider = SupportProvider::Ours;
    // Empty only when Tested.
    std::vector<SupportReason> reasons;
    // The passing record behind a Tested tier.
    std::optional<EvidenceRecord> evidence;

    bool operator==(const SupportCell&) const = default;
};

}  // namespace reboot::support
