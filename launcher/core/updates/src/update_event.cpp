#include "reboot/updates/update_event.hpp"

#include <utility>

namespace rb::updates {

void publish(EventBus& events, UpdateEvent event) {
    const EventKind kind = event_kind(event);
    std::visit([&](auto&& payload) { events.publish(kind, std::move(payload)); }, std::move(event));
}

}  // namespace rb::updates
