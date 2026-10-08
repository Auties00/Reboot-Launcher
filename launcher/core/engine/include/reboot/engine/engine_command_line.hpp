#pragma once

#include <span>
#include <string_view>

#include "reboot/engine/engine_origin.hpp"
#include "reboot/foundation/diag.hpp"

namespace reboot::engine {

// `reboot-engine run [--foreground] [--origin=on-demand|service-manager] [--resume]`.
struct EngineCommandLine {
    EngineOrigin origin = EngineOrigin::OnDemand;
    // Set by the updater's restart.
    bool resume = false;
};

// `args` excludes argv[0]. engine.bad_command_line names the first argument refused.
[[nodiscard]] Result<EngineCommandLine> parse_command_line(std::span<const std::string_view> args);

}  // namespace reboot::engine
