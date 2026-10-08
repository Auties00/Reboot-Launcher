#pragma once

#include <optional>
#include <string_view>

#include "reboot/contracts/ipc.hpp"

namespace reboot::engine {

// Each start path passes its own --origin, so on-demand and at-login need separate units or agents.
using EngineOrigin = contracts::ipc::EngineOrigin;

// Only an engine a client started exits on its own when idle.
[[nodiscard]] constexpr bool exits_when_idle(EngineOrigin origin) noexcept {
    return origin == EngineOrigin::OnDemand;
}

// Drain{Replace} from a client of another build is accepted only by an engine a client started.
[[nodiscard]] constexpr bool replaceable(EngineOrigin origin) noexcept {
    return origin == EngineOrigin::OnDemand;
}

// A client restarting a resident engine passes on-demand; the recorded origin keeps it resident.
[[nodiscard]] constexpr EngineOrigin resumed_origin(EngineOrigin started,
                                                    std::optional<EngineOrigin> recorded) noexcept {
    if (started == EngineOrigin::OnDemand && recorded == EngineOrigin::ServiceManager)
        return EngineOrigin::ServiceManager;
    return started;
}

// The --origin= values; Foreground is chosen by --foreground alone.
[[nodiscard]] constexpr std::optional<EngineOrigin> parse_origin_flag(std::string_view value) noexcept {
    if (value == "on-demand") return EngineOrigin::OnDemand;
    if (value == "service-manager") return EngineOrigin::ServiceManager;
    return std::nullopt;
}

[[nodiscard]] constexpr std::string_view origin_name(EngineOrigin origin) noexcept {
    switch (origin) {
        case EngineOrigin::OnDemand: return "on-demand";
        case EngineOrigin::ServiceManager: return "service-manager";
        case EngineOrigin::Foreground: return "foreground";
    }
    return "unknown";
}

}  // namespace reboot::engine
