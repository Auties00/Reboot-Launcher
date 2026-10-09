#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::support {

// Declared from best to worst, so combining two tiers keeps the larger one.
enum class SupportTier : u8 { Tested, Untested, Blocked };

[[nodiscard]] constexpr SupportTier worst(SupportTier a, SupportTier b) noexcept { return a < b ? b : a; }

}  // namespace rb::support
