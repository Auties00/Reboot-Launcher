#pragma once

#include <cstddef>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::sessions {

// In run order; FlushLogs stays last.
enum class ShutdownStep : u8 {
    RefuseNew,
    Unpublish,
    UnmapPorts,
    DrainHosts,
    StopPlay,
    StopBackend,
    CloseListeners,
    FlushStores,
    FlushLogs,
};

inline constexpr std::size_t kShutdownStepCount = static_cast<std::size_t>(ShutdownStep::FlushLogs) + 1;

// These keep their own budget even after the total ran out: skipping them loses data.
[[nodiscard]] constexpr bool always_runs(ShutdownStep step) noexcept {
    return step == ShutdownStep::FlushStores || step == ShutdownStep::FlushLogs;
}

[[nodiscard]] constexpr std::string_view shutdown_step_name(ShutdownStep step) noexcept {
    switch (step) {
        case ShutdownStep::RefuseNew: return "refuse_new";
        case ShutdownStep::Unpublish: return "unpublish";
        case ShutdownStep::UnmapPorts: return "unmap_ports";
        case ShutdownStep::DrainHosts: return "drain_hosts";
        case ShutdownStep::StopPlay: return "stop_play";
        case ShutdownStep::StopBackend: return "stop_backend";
        case ShutdownStep::CloseListeners: return "close_listeners";
        case ShutdownStep::FlushStores: return "flush_stores";
        case ShutdownStep::FlushLogs: return "flush_logs";
    }
    return "unknown";
}

}  // namespace reboot::sessions
