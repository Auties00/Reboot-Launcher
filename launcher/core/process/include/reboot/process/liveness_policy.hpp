#pragma once

#include <chrono>

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::process {

// Covers no capability ids. A Ping every `interval`; `miss_limit` intervals in a row without a
// Pong is a hang.
struct LivenessPolicy {
    std::chrono::milliseconds interval = kLivenessPingInterval;
    u32 miss_limit = static_cast<u32>(kLivenessMissLimit);
};

}  // namespace reboot::process
