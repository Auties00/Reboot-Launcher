#pragma once

#include <string>

#include "reboot/ports/runner.hpp"

namespace reboot::support {

// `runtime_id` is the pinned runtime component id; empty for Native.
struct RunnerPin {
    ports::RunnerKind runner = ports::RunnerKind::Native;
    std::string runtime_id;

    bool operator==(const RunnerPin&) const = default;
};

}  // namespace reboot::support
