#pragma once

#include <optional>

#include "reboot/foundation/types.hpp"

namespace rb::host {

// Reset: a Host reset rewrote every profile.
enum class HostProfileChange : u8 { Added, Updated, Removed, Reset };

// EventKind::HostProfilesChanged, coalesced per profile; the profiles themselves are re-read.
struct HostProfilesChanged {
    HostProfileChange change = HostProfileChange::Updated;
    // Absent for Reset.
    std::optional<HostProfileId> profile;
};

}  // namespace rb::host
