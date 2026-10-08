#pragma once

#include <cstddef>
#include <optional>

#include "reboot/backend/backend_state.hpp"
#include "reboot/backend/backend_stop_cause.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::backend {

enum class BackendChange : u8 { Starting, Running, Crashed, Restarting, Stopping, Stopped, Failed, Reconfigured, LeasesChanged };

// EventKind::BackendStateChanged, coalesced to the latest; `state` is complete, so a dropped
// change loses nothing a subscriber cannot read from the next one.
struct BackendEvent {
    BackendChange change = BackendChange::Stopped;
    BackendState state;
    // Set with Stopped and Stopping.
    std::optional<BackendStopCause> stop_cause;

    [[nodiscard]] std::size_t approx_bytes() const noexcept;
};

}  // namespace reboot::backend
