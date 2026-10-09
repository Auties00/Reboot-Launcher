#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "reboot/browser/server_row.hpp"
#include "reboot/browser/view_spec.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class IClock;
class TimerService;
}  // namespace rb

namespace rb::browser {

class BrowserSession;
class IOwnServers;

inline constexpr std::chrono::milliseconds kSearchDebounce{250};

// Opaque edge cursor; empty means the first page.
struct SearchCursor {
    std::vector<u8> bytes;

    bool operator==(const SearchCursor&) const = default;
};

struct SearchRequest {
    // Trimmed; 1 to 64 bytes. A reboot:// link or a bare server id is resolved by id instead.
    std::string text;
    // One bucket, 0 for every version: the edge's cursor pages a single bucket.
    u32 bucket = 0;
    PasswordFilter password = PasswordFilter::Any;
    Region region = Region::All;
    ServerSort sort = ServerSort::Players;
    SearchCursor cursor;
    // 0 is the edge's default page; larger values are clamped to Welcome.limits.max_query_limit.
    u32 limit = 0;
};

// A page is a one-off answer, not live; scrolling or new text runs another search.
struct SearchPage {
    std::vector<ServerRow> rows;
    // Absent on the last page.
    std::optional<SearchCursor> next;
    // The edge's count, which still includes this user's own servers that `rows` leave out.
    u32 total = 0;
    // The text was a link or an id; hidden and offline servers answer too.
    bool resolved_by_id = false;
};

// Capabilities: server-browser.+27.
// Strand-only. rbsb/1 Query on name, author or id with ASCII case folding, paced at the edge's
// query rate; each client's searches are debounced on their own.
class Search {
public:
    Search(BrowserSession& session, const IOwnServers& own, TimerService& timers, const IClock& clock);
    ~Search();
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    // Starts kSearchDebounce after the client's last call; a newer call from the same client
    // cancels its pending one with CancelReason::Superseded. Fails synchronously with
    // browser.search_text_length. `done` runs on the strand exactly once.
    [[nodiscard]] Result<void> run(ConnectionId client, SearchRequest request, CancelToken token,
                                   UniqueFunction<void(Result<SearchPage>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::browser
