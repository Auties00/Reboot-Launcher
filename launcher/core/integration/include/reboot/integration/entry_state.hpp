#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::integration {

enum class EntryState : u8 {
    Absent,
    Ours,
    // Ours, but the user turned it off in the OS (Startup apps, Hidden=true); left as they set it.
    Disabled,
    // Ours, but macOS Login Items waits for the user to allow it.
    AwaitingApproval,
    // Another app or another install owns it; it is never overwritten or removed.
    Foreign,
    // Ours, but for an older copy or with arguments that differ from the current ones.
    Stale,
    // This OS or install has no such entry, e.g. no GUI to handle reboot:// links.
    Unsupported,
    // Inspection failed; `detail` says why.
    Unknown,
};

}  // namespace rb::integration
