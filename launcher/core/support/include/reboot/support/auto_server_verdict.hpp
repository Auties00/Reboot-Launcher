#pragma once

#include "reboot/support/support_tier.hpp"
#include "reboot/support/support_verdict.hpp"

namespace rb::support {

// Play with the linked auto server is never rated above the server's host cell.
struct AutoServerVerdict {
    SupportVerdict play;
    SupportVerdict host;
    SupportTier tier = SupportTier::Untested;

    bool operator==(const AutoServerVerdict&) const = default;
};

}  // namespace rb::support
