#pragma once

#include <string>

#include "reboot/compat/runner_profile.hpp"
#include "reboot/compat/runtime_id.hpp"

namespace rb::compat {

// What the runtime that last booted a prefix left in it.
struct PrefixRecord {
    RunnerKind kind{};
    RuntimeId runtime;
    std::string runtime_version;
    bool vc_runtime_seeded = false;

    bool operator==(const PrefixRecord&) const = default;
};

}  // namespace rb::compat
