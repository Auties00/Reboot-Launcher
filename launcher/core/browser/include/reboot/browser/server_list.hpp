#pragma once

#include <memory>
#include <vector>

#include "reboot/browser/browse_choices.hpp"
#include "reboot/browser/list_state.hpp"
#include "reboot/browser/view_spec.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class EventBus;
class IClock;
}  // namespace rb

namespace rb::browser {

class BrowserSession;
class IOwnServers;

// Capabilities: server-browser.list-state, server-browser.browse, server-browser.+28, server-browser.+29, server-browser.+62, server-browser.+65, server-browser.+88.
// Strand-only. Live views, one subscription per bucket (every version past the edge's limit),
// merged and filtered locally; publishes ViewSnapshot on state changes and ViewDelta on rows.
class ServerList {
public:
    using PersistChoices = UniqueFunction<void(const BrowseChoices&)>;

    ServerList(BrowserSession& session, const IOwnServers& own, const IClock& clock, EventBus& events,
               BrowseChoices saved, PersistChoices persist);
    ~ServerList();
    ServerList(const ServerList&) = delete;
    ServerList& operator=(const ServerList&) = delete;

    // Fails with ViewSpec::validate's error. The view starts Loading.
    [[nodiscard]] Result<ViewId> open(const ViewSpec& spec);
    // Re-subscribes only the buckets that changed; the view goes back to Loading if any did.
    [[nodiscard]] Result<void> update(ViewId view, const ViewSpec& spec);
    // Unknown ids are ignored.
    void close(ViewId view);

    // Null for an unknown id.
    [[nodiscard]] const ViewUpdate* current(ViewId view) const;
    [[nodiscard]] std::vector<ViewId> open_views() const;

    [[nodiscard]] const BrowseChoices& choices() const noexcept;
    // Stored and handed to the persist callback; open views keep their own spec.
    void set_choices(const BrowseChoices& choices);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::browser
