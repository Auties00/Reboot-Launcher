#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/support/support_query.hpp"
#include "reboot/support/support_verdict.hpp"

namespace rb::host {

// The payload of UserRequestKind::ConfirmUntested raised by HostService::start; the answer is a
// bool, true to host anyway.
struct UntestedHostPrompt {
    HostProfileId profile;
    support::SupportQuery query;
    support::SupportVerdict verdict;
};

}  // namespace rb::host
