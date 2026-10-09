#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

#include "reboot/browser/server_row.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::browser {

using ViewId = Counter<struct BrowserViewTag, u64>;

// Loading: no snapshot yet. Ready: live. Stale: the connection dropped; the rows are the last
// ones seen and the next snapshot replaces them, never merges into them.
enum class ListState : u8 { Loading, Ready, Stale };

// Capabilities: server-browser.list-state.
// Payload of EventKind::ViewSnapshot (state changes) and EventKind::ViewDelta (row changes,
// coalesced per view); both carry the whole window.
struct ViewUpdate {
    ViewId view;
    ListState state = ListState::Loading;
    // Drives "N servers": the rows kept when every match fits in the window, else the edge's count
    // over the buckets, which still includes servers the local filters would drop.
    u32 total = 0;
    bool total_approximate = false;
    // In the view's sort order, after the local filters.
    std::vector<ServerRow> rows;
    std::optional<std::chrono::system_clock::time_point> stale_since;

    [[nodiscard]] std::size_t approx_bytes() const noexcept;
};

}  // namespace rb::browser
