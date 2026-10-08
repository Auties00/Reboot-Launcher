#pragma once

#include <optional>

#include "reboot/ports/session_host.hpp"

namespace reboot::injection {

// Capabilities: dll-injection.timing.
// A runtime's boot_inject default, from the runtime manifest entry the session pins, so a
// runtime bump can promote or demote it without an engine release.
struct RuntimeBootDefault {
    // Set once the early-bird spike passed on this runtime; until then AfterResume is forced.
    std::optional<ports::BootStrategy> proven;

    bool operator==(const RuntimeBootDefault&) const = default;
};

// Native Windows has no runtime manifest entry and passed the spike.
inline constexpr RuntimeBootDefault kNativeBootDefault{ports::BootStrategy::EarlyBirdApc};

// `build` is the catalog's boot_inject for this build on the session's runner.
[[nodiscard]] constexpr ports::BootStrategy resolve_boot_strategy(std::optional<ports::BootStrategy> build,
                                                                  RuntimeBootDefault runtime) noexcept {
    if (!runtime.proven) return ports::BootStrategy::AfterResume;
    return build.value_or(*runtime.proven);
}

}  // namespace reboot::injection
