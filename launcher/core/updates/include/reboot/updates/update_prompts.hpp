#pragma once

#include <vector>

#include "reboot/foundation/version.hpp"
#include "reboot/updates/activity_probe.hpp"

namespace reboot::updates {

// Payload of UserRequestKind::ConfirmStopSessions raised by Updates.apply(now).
struct ConfirmStopSessionsPrompt {
    SemVer version;
    std::vector<LiveActivity> live;
};

// The only accepted answer; accepting starts Drain{Update}, declining leaves the update staged.
struct ConfirmStopSessionsAnswer {
    bool accept = false;
};

}  // namespace reboot::updates
