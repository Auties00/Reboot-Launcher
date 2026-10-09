#include "reboot/browser/server_list.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "client/view_mirror.hpp"
#include "messages.hpp"
#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/browser/version_match.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "wire_mapping.hpp"

namespace reboot::browser {

namespace {

namespace wire = sb::wire;

[[nodiscard]] std::vector<u32> distinct(std::vector<u32> buckets) {
    std::ranges::sort(buckets);
    const auto duplicates = std::ranges::unique(buckets);
    buckets.erase(duplicates.begin(), duplicates.end());
    return buckets;
}

}  // namespace

struct ServerList::Impl {
    // One subscription of a view: a bucket, or bucket 0 for every version.
    struct Feed {
        u64 id = 0;
        ViewSubscription subscription;
        sb::client::ViewMirror mirror;
        // Has a snapshot from the current connection.
        bool fresh = false;
    };

    struct View {
        ViewSpec spec;
        // Too many buckets for the edge's subscription limit: bucket 0, filtered here.
        bool filter_locally = false;
        std::map<u32, Feed> feeds;
        ViewUpdate update;
    };

    Impl(BrowserSession& session_in, const IOwnServers& own_in, const IClock& clock_in, EventBus& events_in,
         BrowseChoices saved, PersistChoices persist_in)
        : session(session_in), own(own_in), clock(clock_in), events(events_in), choices(saved),
          persist(std::move(persist_in)) {}

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    ~Impl() {
        // Subscriptions go first, while the views they point into still exist.
        for (auto& [id, view] : views) view.feeds.clear();
    }

    [[nodiscard]] u32 window_of(const ViewSpec& spec) const {
        return effective_window(spec.window, session.edge() ? session.edge()->limits.max_window : 0);
    }

    [[nodiscard]] std::vector<u32> wanted_buckets(const View& view) const {
        if (view.filter_locally || view.spec.versions.buckets.empty()) return {wire::kBucketAll};
        return distinct(view.spec.versions.buckets);
    }

    [[nodiscard]] wire::ViewSpec wire_spec(const ViewSpec& spec, u32 bucket) const {
        return wire::ViewSpec{bucket, to_wire(spec.password), to_wire(spec.region), to_wire(spec.sort)};
    }

    // Subscribes every wanted bucket that has no feed yet; on the edge's subscription limit the
    // view falls back to one bucket-0 feed filtered here.
    Result<void> subscribe_missing(ViewId id, View& view) {
        // Dropping feeds on a fallback must not close the connection the new ones need.
        const BrowserLease hold = session.acquire();
        for (const u32 bucket : wanted_buckets(view)) {
            if (view.feeds.contains(bucket)) continue;
            const u64 feed_id = next_feed++;
            Result<ViewSubscription> subscription =
                session.subscribe(wire_spec(view.spec, bucket), window_of(view.spec), callbacks(id, bucket, feed_id));
            if (!subscription) {
                if (view.filter_locally) return std::unexpected(std::move(subscription.error()));
                fall_back(view);
                return subscribe_missing(id, view);
            }
            Feed& feed = view.feeds[bucket];
            feed.id = feed_id;
            feed.subscription = std::move(*subscription);
        }
        return {};
    }

    static void fall_back(View& view) {
        view.filter_locally = true;
        // Moved out first: a reset may run session callbacks that look the feeds up.
        std::map<u32, Feed> dropped = std::move(view.feeds);
        view.feeds.clear();
        dropped.clear();
    }

    [[nodiscard]] ViewStreamCallbacks callbacks(ViewId id, u32 bucket, u64 feed_id) {
        ViewStreamCallbacks out;
        out.on_snapshot = [this, id, bucket, feed_id](const wire::Snapshot& snapshot) {
            Feed* feed = find_feed(id, bucket, feed_id);
            if (feed == nullptr) return;
            feed->mirror.on_snapshot(snapshot);
            feed->fresh = true;
            refresh(id);
        };
        out.on_delta = [this, id, bucket, feed_id](const wire::Delta& delta) {
            Feed* feed = find_feed(id, bucket, feed_id);
            if (feed == nullptr) return;
            feed->mirror.on_delta(delta);
            if (feed->fresh) refresh(id);
        };
        out.on_lost = [this, id, bucket, feed_id] {
            Feed* feed = find_feed(id, bucket, feed_id);
            if (feed == nullptr) return;
            // Handles die with the connection; the next snapshot starts a new mirror.
            feed->mirror = sb::client::ViewMirror();
            feed->fresh = false;
            go_stale(id);
        };
        out.on_rejected = [this, id, bucket, feed_id](const RbsbRequestError& error) {
            const auto it = views.find(id);
            if (it == views.end() || find_feed(id, bucket, feed_id) == nullptr) return;
            View& view = it->second;
            if (view.filter_locally || error.code != wire::ErrorCode::limit_exceeded) return;
            const BrowserLease hold = session.acquire();
            fall_back(view);
            if (Result<void> subscribed = subscribe_missing(id, view); subscribed) set_loading(id);
        };
        return out;
    }

    Feed* find_feed(ViewId id, u32 bucket, u64 feed_id) {
        const auto view = views.find(id);
        if (view == views.end()) return nullptr;
        const auto feed = view->second.feeds.find(bucket);
        if (feed == view->second.feeds.end() || feed->second.id != feed_id) return nullptr;
        return &feed->second;
    }

    void publish(ViewId id, EventKind kind) {
        const View& view = views.at(id);
        events.publish(kind, view.update, EventScope{std::nullopt, std::nullopt, std::to_string(id.value)});
    }

    void set_loading(ViewId id) {
        ViewUpdate& update = views.at(id).update;
        update = ViewUpdate{id, ListState::Loading, 0, false, {}, std::nullopt};
        publish(id, EventKind::ViewSnapshot);
    }

    void go_stale(ViewId id) {
        ViewUpdate& update = views.at(id).update;
        // A view that never loaded has nothing stale to show.
        if (update.state != ListState::Ready) return;
        update.state = ListState::Stale;
        update.stale_since = clock.system_now();
        publish(id, EventKind::ViewSnapshot);
    }

    // Rebuilds the rows once every feed has a snapshot from the current connection.
    void refresh(ViewId id) {
        View& view = views.at(id);
        if (view.feeds.empty()) return;
        for (const auto& [bucket, feed] : view.feeds)
            if (!feed.fresh) return;

        const u32 window = window_of(view.spec);
        const wire::Sort sort = to_wire(view.spec.sort);
        const std::vector<u32> wanted = distinct(view.spec.versions.buckets);
        bool complete = true;
        u32 edge_total = 0;
        std::vector<wire::ListEntry> entries;
        std::set<ServerId> listed;
        for (const auto& [bucket, feed] : view.feeds) {
            edge_total += feed.mirror.total();
            if (feed.mirror.total() > window) complete = false;
            for (wire::ListEntry& entry : feed.mirror.sorted(sort)) {
                if (own.owns(ServerId{entry.id})) continue;
                if (view.filter_locally && !wanted.empty() && !std::ranges::binary_search(wanted, entry.bucket)) continue;
                if (view.spec.versions.exact_version && !same_game_version(entry.version, *view.spec.versions.exact_version))
                    continue;
                // A server in two buckets' views is listed once.
                if (!listed.insert(ServerId{entry.id}).second) continue;
                entries.push_back(std::move(entry));
            }
        }
        std::ranges::sort(entries, [sort](const wire::ListEntry& a, const wire::ListEntry& b) {
            return sb::client::ViewMirror::less(sort, a, b);
        });
        const auto matches = static_cast<u32>(entries.size());
        if (entries.size() > window) entries.resize(window);

        const std::chrono::milliseconds offset = session.edge() ? session.edge()->clock_offset : std::chrono::milliseconds{0};
        ViewUpdate next{id, ListState::Ready, complete ? matches : edge_total, !complete, {}, std::nullopt};
        next.rows.reserve(entries.size());
        for (const wire::ListEntry& entry : entries) next.rows.push_back(make_server_row(entry, offset));

        ViewUpdate& current = view.update;
        const bool state_changed = current.state != ListState::Ready;
        const bool rows_changed =
            current.rows != next.rows || current.total != next.total || current.total_approximate != next.total_approximate;
        current = std::move(next);
        if (state_changed) publish(id, EventKind::ViewSnapshot);
        else if (rows_changed) publish(id, EventKind::ViewDelta);
    }

    BrowserSession& session;
    const IOwnServers& own;
    const IClock& clock;
    EventBus& events;
    BrowseChoices choices;
    PersistChoices persist;
    u64 next_view = 1;
    u64 next_feed = 1;
    std::map<ViewId, View> views;
};

ServerList::ServerList(BrowserSession& session, const IOwnServers& own, const IClock& clock, EventBus& events,
                       BrowseChoices saved, PersistChoices persist)
    : impl_(std::make_unique<Impl>(session, own, clock, events, saved, std::move(persist))) {}

ServerList::~ServerList() = default;

Result<ViewId> ServerList::open(const ViewSpec& spec) {
    if (Result<void> valid = spec.validate(); !valid) return std::unexpected(std::move(valid.error()));
    const ViewId id{impl_->next_view++};
    Impl::View& view = impl_->views[id];
    view.spec = spec;
    view.update.view = id;
    if (Result<void> subscribed = impl_->subscribe_missing(id, view); !subscribed) {
        impl_->views.erase(id);
        return std::unexpected(std::move(subscribed.error()));
    }
    impl_->publish(id, EventKind::ViewSnapshot);
    return id;
}

Result<void> ServerList::update(ViewId id, const ViewSpec& spec) {
    if (Result<void> valid = spec.validate(); !valid) return valid;
    const auto it = impl_->views.find(id);
    if (it == impl_->views.end())
        return make_diag(ErrorDomain::Browser, kInvalidViewSpec).arg("field", "view").kind(ErrorKind::NotFound).fail();
    Impl::View& view = it->second;
    // Replaced feeds must not close the connection their successors use.
    const BrowserLease hold = impl_->session.acquire();
    const ViewSpec previous = view.spec;
    view.spec = spec;

    const bool same_feeds = previous.password == spec.password && previous.region == spec.region &&
                            previous.sort == spec.sort && impl_->window_of(previous) == impl_->window_of(spec);
    if (distinct(previous.versions.buckets) != distinct(spec.versions.buckets)) view.filter_locally = false;
    const std::vector<u32> wanted = impl_->wanted_buckets(view);
    bool changed = false;
    for (auto feed = view.feeds.begin(); feed != view.feeds.end();) {
        if (same_feeds && std::ranges::find(wanted, feed->first) != wanted.end()) {
            ++feed;
            continue;
        }
        feed = view.feeds.erase(feed);
        changed = true;
    }
    const u64 feeds_before = impl_->next_feed;
    if (Result<void> subscribed = impl_->subscribe_missing(id, view); !subscribed) {
        impl_->set_loading(id);
        return subscribed;
    }
    changed = changed || impl_->next_feed != feeds_before;
    if (changed) {
        impl_->set_loading(id);
        return {};
    }
    // Only local filters changed: the feeds stay and the rows are rebuilt.
    impl_->refresh(id);
    return {};
}

void ServerList::close(ViewId id) {
    const auto it = impl_->views.find(id);
    if (it == impl_->views.end()) return;
    std::map<u32, Impl::Feed> feeds = std::move(it->second.feeds);
    impl_->views.erase(it);
    feeds.clear();
}

const ViewUpdate* ServerList::current(ViewId id) const {
    const auto it = impl_->views.find(id);
    return it == impl_->views.end() ? nullptr : &it->second.update;
}

std::vector<ViewId> ServerList::open_views() const {
    std::vector<ViewId> out;
    out.reserve(impl_->views.size());
    for (const auto& [id, view] : impl_->views) out.push_back(id);
    return out;
}

const BrowseChoices& ServerList::choices() const noexcept { return impl_->choices; }

void ServerList::set_choices(const BrowseChoices& choices) {
    impl_->choices = choices;
    if (impl_->persist) impl_->persist(impl_->choices);
}

}  // namespace reboot::browser
