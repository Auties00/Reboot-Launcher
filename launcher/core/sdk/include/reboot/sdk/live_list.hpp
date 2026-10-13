#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/api/v1/common.hpp"
#include "reboot/api/v1/sessions.hpp"
#include "reboot/sdk/context.hpp"
#include "reboot/sdk/dispatcher.hpp"
#include "reboot/sdk/error.hpp"
#include "reboot/sdk/event.hpp"
#include "reboot/sdk/subscription.hpp"

namespace rb::sdk {

// Where a LiveList's items come from: a snapshot call, then the events that change them.
template <class T>
struct LiveListSource {
    // The kinds whose events carry changes; Resync, ConnectionLost and Reconnected always arrive.
    api::EventFilter filter;
    std::function<Expected<std::vector<T>>(const Context&)> snapshot;
    // Applies one change; false when the change cannot be applied in place, which takes a new snapshot.
    std::function<bool(std::vector<T>&, const Event&)> apply;
};

// A list kept current from a snapshot plus deltas. A Resync or Reconnected takes a new snapshot, and
// a ConnectionLost marks the list stale until then. Read it on the dispatcher's thread; `on_change`
// runs there after every change.
template <class T>
class LiveList {
public:
    [[nodiscard]] static Expected<LiveList> open(const Context& context, Dispatcher& dispatcher, LiveListSource<T> source,
                                                 std::function<void()> on_change = {}) {
        // Subscribed first, so no change between the snapshot and the first delta is lost.
        Expected<Subscription> events = context.subscribe(source.filter);
        if (!events) return std::unexpected(std::move(events.error()));
        auto state = std::make_shared<State>(context, std::move(source), std::move(on_change));
        Expected<std::vector<T>> first = state->source.snapshot(context);
        if (!first) return std::unexpected(std::move(first.error()));
        state->items = std::move(*first);
        state->snapshots = 1;
        events->deliver(dispatcher, [weak = std::weak_ptr<State>(state)](const Event& event) {
            if (const std::shared_ptr<State> live = weak.lock()) live->on_event(event);
        });
        return LiveList(std::move(state), std::move(*events));
    }

    [[nodiscard]] const std::vector<T>& items() const noexcept { return state_->items; }
    // True from a ConnectionLost, or a failed snapshot, until the next good snapshot.
    [[nodiscard]] bool stale() const noexcept { return state_->stale; }
    // Why the last snapshot failed.
    [[nodiscard]] const std::optional<Error>& last_error() const noexcept { return state_->last_error; }
    [[nodiscard]] std::uint64_t snapshots() const noexcept { return state_->snapshots; }

private:
    struct State {
        State(const Context& owner, LiveListSource<T> from, std::function<void()> changed)
            : context(owner), source(std::move(from)), on_change(std::move(changed)) {}

        void on_event(const Event& event) {
            if (event.get<ConnectionLost>() != nullptr || event.get<Closed>() != nullptr) {
                stale = true;
            } else if (event.get<Resync>() != nullptr || event.get<Reconnected>() != nullptr ||
                       !source.apply(items, event)) {
                resnapshot();
            }
            if (on_change) on_change();
        }

        void resnapshot() {
            Expected<std::vector<T>> fresh = source.snapshot(context);
            if (!fresh) {
                stale = true;
                last_error = std::move(fresh.error());
                return;
            }
            items = std::move(*fresh);
            stale = false;
            last_error.reset();
            ++snapshots;
        }

        Context context;
        LiveListSource<T> source;
        std::function<void()> on_change;
        std::vector<T> items;
        bool stale = false;
        std::optional<Error> last_error;
        std::uint64_t snapshots = 0;
    };

    LiveList(std::shared_ptr<State> state, Subscription events) : state_(std::move(state)), events_(std::move(events)) {}

    std::shared_ptr<State> state_;
    Subscription events_;
};

// The engine's sessions: Sessions.list, kept current by SessionStateChanged and SessionEnded.
[[nodiscard]] inline LiveListSource<api::SessionSummary> sessions_source() {
    LiveListSource<api::SessionSummary> source;
    source.filter.kinds = {api::EventKind::SessionStateChanged, api::EventKind::SessionEnded};
    source.snapshot = [](const Context& context) -> Expected<std::vector<api::SessionSummary>> {
        Expected<api::SessionsListResponse> listed = context.sessions_list();
        if (!listed) return std::unexpected(std::move(listed.error()));
        return std::move(listed->sessions);
    };
    source.apply = [](std::vector<api::SessionSummary>& sessions, const Event& event) {
        const auto find = [&sessions](const Uuid& id) {
            return std::ranges::find_if(sessions, [&id](const api::SessionSummary& session) { return session.id.uuid == id; });
        };
        if (const auto* changed = event.get<api::SessionStateChanged>()) {
            const api::SessionSummary& summary = changed->session;
            const auto found = find(summary.id.uuid);
            if (summary.phase == api::SessionPhase::Ended) {
                if (found != sessions.end()) sessions.erase(found);
            } else if (found != sessions.end()) {
                *found = summary;
            } else {
                sessions.push_back(summary);
            }
            return true;
        }
        if (event.get<api::SessionEnded>() != nullptr) {
            if (!event.session) return false;
            if (const auto found = find(*event.session); found != sessions.end()) sessions.erase(found);
        }
        return true;
    };
    return source;
}

}  // namespace rb::sdk
