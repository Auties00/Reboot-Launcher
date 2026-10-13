#pragma once

#include <chrono>

#include "reboot/base/types.hpp"

namespace rb::net {

// One of the engine's live children; `created` guards against a reused pid.
struct OurProcess {
    u32 pid = 0;
    std::chrono::system_clock::time_point created;
};

}  // namespace rb::net
