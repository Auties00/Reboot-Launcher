#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "reboot/browser/browser_session.hpp"
#include "reboot/components/component_problem.hpp"
#include "reboot/engine/api_router.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/prerequisite.hpp"
#include "reboot/ports/os_services.hpp"
#include "reboot/ux/message_catalog.hpp"

namespace rb::engine {

// What ApiRouter keeps besides the connections and started ops. Strand-only.
struct ApiRouter::State {
    // An op that ends with another op's outcome, such as a purge that first stops the sessions.
    struct Relay {
        std::shared_ptr<Subscription> inner_events;
        CancelRegistration cancel;
    };

    // Each open view's owner.
    FlatMap<u64, ConnectionId> views;
    // Held while any view is open, so the browse connection lives exactly as long as the views.
    browser::BrowserLease browser_lease;
    // Each running search's hold on the browse connection; the router outlives none of them.
    FlatMap<u64, browser::BrowserLease> search_leases;
    u64 next_search = 0;
    FlatMap<std::string, std::vector<u8>> frontend;
    FlatSet<std::string> frontend_reading;
    // A failed read, answered once by the shell's next frontend_state_get, which then reads again.
    FlatMap<std::string, Diagnostic> frontend_errors;
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
