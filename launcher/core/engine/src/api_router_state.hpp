#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "reboot/browser/browser_session.hpp"
#include "reboot/components/component_problem.hpp"
#include "reboot/engine/api_router.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/prerequisite.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ux/message_catalog.hpp"

namespace rb::engine {

// What ApiRouter keeps besides the connections and started ops. Strand-only.
struct ApiRouter::State {
    struct View {
        ConnectionId owner;
        // Free-text search, matched locally against the name and the author.
        std::string text;
    };

    // An op that ends with another op's outcome, such as a purge that first stops the sessions.
    struct Relay {
        std::shared_ptr<Subscription> inner_events;
        CancelRegistration cancel;
    };

    FlatMap<u64, View> views;
    // Held while any view is open, so the browse connection lives exactly as long as the views.
    browser::BrowserLease browser_lease;
    FlatMap<std::string, std::vector<u8>> frontend;
    std::optional<std::vector<integration::EntryStatus>> integration;
    std::optional<std::vector<integration::Prerequisite>> prerequisites;
    std::optional<ports::SecurityProducts> security;
    bool integration_reading = false;
    bool prerequisites_reading = false;
    bool security_reading = false;
    // The last problem each component reported, until it recovered.
    FlatMap<std::string, components::ComponentProblem> component_problems;
    std::shared_ptr<Subscription> watch;
    std::optional<ux::MessageCatalog> catalog;
    FlatMap<OpId, Relay> relays;
    // Cancelled first in the destructor, so replies that arrive later never touch the router.
    CancelSource alive;
};

}  // namespace rb::engine
