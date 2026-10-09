#pragma once

#include <chrono>
#include <type_traits>
#include <variant>
#include <vector>

#include "reboot/components/app_entry.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/updates/activity_probe.hpp"
#include "reboot/updates/pending_update_marker.hpp"
#include "reboot/updates/update_offer.hpp"

namespace rb::updates {

// EventKind::UpdateAvailable.
struct UpdateAvailable {
    UpdateOffer offer;
};

// EventKind::UpdateStaged. `blocking` is empty when the apply follows at once.
struct UpdateStaged {
    components::AppEntry update;
    std::vector<LiveActivity> blocking;
};

// EventKind::EngineUpdating: the engine restarts into `version`; clients exit or reconnect.
struct EngineUpdating {
    SemVer version;
    // The longest clients wait for the new engine before starting one themselves.
    std::chrono::seconds eta = kUpdateMarkerTimeout;
};

enum class UpdateStage : u8 { Check, Download, Verify, Stage, Apply, Confirm };

// EventKind::UpdateFailed.
struct UpdateFailed {
    UpdateStage stage{};
    Diagnostic error;
};

using UpdateEvent = std::variant<UpdateAvailable, UpdateStaged, EngineUpdating, UpdateFailed>;

[[nodiscard]] inline EventKind event_kind(const UpdateEvent& event) noexcept {
    return std::visit(
        []<class E>(const E&) {
            if constexpr (std::is_same_v<E, UpdateAvailable>) return EventKind::UpdateAvailable;
            else if constexpr (std::is_same_v<E, UpdateStaged>) return EventKind::UpdateStaged;
            else if constexpr (std::is_same_v<E, EngineUpdating>) return EventKind::EngineUpdating;
            else return EventKind::UpdateFailed;
        },
        event);
}

// Publishes the alternative itself as the payload, under its kind.
void publish(EventBus& events, UpdateEvent event);

}  // namespace rb::updates
