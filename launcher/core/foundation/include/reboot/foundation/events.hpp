#pragma once

#include <any>
#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {

enum class EventKind : u16 {
    EngineState,
    SettingsChanged,
    LibraryChanged,
    CatalogChanged,
    ComponentChanged,
    IdentityChanged,
    OpProgress,
    OpCompleted,
    SessionStateChanged,
    SessionSpawned,
    SessionDegraded,
    SessionEnded,
    HostPhaseChanged,
    HostListening,
    ReachabilityChanged,
    PortMappingChanged,
    PublishStateChanged,
    MatchEvent,
    PlayerEvent,
    BackendStateChanged,
    XmppUnavailable,
    BrowserConnectionChanged,
    ViewSnapshot,
    ViewDelta,
    JoinTargetChanged,
    UpdateAvailable,
    UpdateStaged,
    EngineUpdating,
    UpdateFailed,
    NoticeAdded,
    LogLine,
    UserActionRequired,
    UserActionResolved,
    ForegroundHint,
    OpStarted,
    HostProfilesChanged,
    BackendAccountsChanged,
    NoticeRemoved,
    OnboardingChanged,
    IntegrationChanged,
    PrerequisitesChanged,
    SecretStateChanged,
    StorageModeChanged,
};

enum class DeliveryClass : u8 { NeverDrop, CoalesceLatest, DropWithCounter };

// CoalesceLatest keeps only the newest queued event per (kind, coalesce_key).
[[nodiscard]] constexpr DeliveryClass delivery_class(EventKind kind) noexcept {
    switch (kind) {
        case EventKind::EngineState:
        case EventKind::SettingsChanged:
        case EventKind::LibraryChanged:
        case EventKind::CatalogChanged:
        case EventKind::ComponentChanged:
        case EventKind::IdentityChanged:
        case EventKind::OpProgress:
        case EventKind::HostPhaseChanged:
        case EventKind::ReachabilityChanged:
        case EventKind::PortMappingChanged:
        case EventKind::PublishStateChanged:
        case EventKind::BackendStateChanged:
        case EventKind::BrowserConnectionChanged:
        case EventKind::ViewDelta:
        case EventKind::JoinTargetChanged:
        case EventKind::HostProfilesChanged:
        case EventKind::BackendAccountsChanged:
        case EventKind::OnboardingChanged:
        case EventKind::IntegrationChanged:
        case EventKind::PrerequisitesChanged:
        case EventKind::SecretStateChanged:
        case EventKind::StorageModeChanged:
            return DeliveryClass::CoalesceLatest;
        case EventKind::LogLine:
            return DeliveryClass::DropWithCounter;
        default:
            return DeliveryClass::NeverDrop;
    }
}

struct EventEnvelope {
    EventKind kind{};
    EngineEpoch epoch;
    u64 seq = 0;
    std::optional<SessionId> session;
    std::optional<OpId> op;
    std::string coalesce_key;
    std::any payload;
    // Queue-budget estimate; the payload is not serialised until the IPC writer encodes it.
    std::size_t approx_bytes = 0;
};

struct EventScope {
    std::optional<SessionId> session;
    std::optional<OpId> op;
    std::string coalesce_key;
};

// An empty `kinds` matches every kind.
struct EventFilter {
    std::vector<EventKind> kinds;
    std::optional<SessionId> session;
    std::optional<OpId> op;
};

// Strand-only. When a NeverDrop event does not fit, the queue is cleared and a Resync is
// pending; the reader re-fetches state and calls take_resync().
class Subscription {
public:
    Subscription(EventFilter filter, std::size_t byte_budget);

    std::size_t drain(std::vector<EventEnvelope>& out, std::size_t max);
    [[nodiscard]] bool resync_pending() const noexcept { return resync_; }
    bool take_resync() noexcept { return std::exchange(resync_, false); }
    [[nodiscard]] u64 dropped() const noexcept { return dropped_; }
    [[nodiscard]] const EventFilter& filter() const noexcept { return filter_; }

    // Called when the queue goes from empty to non-empty, or a Resync becomes pending.
    void set_notify(UniqueFunction<void()> notify) { notify_ = std::move(notify); }

private:
    friend class EventBus;
    [[nodiscard]] bool matches(const EventEnvelope& event) const;
    void offer(const EventEnvelope& event);

    EventFilter filter_;
    std::size_t byte_budget_;
    std::size_t used_bytes_ = 0;
    std::deque<EventEnvelope> queue_;
    bool resync_ = false;
    u64 dropped_ = 0;
    UniqueFunction<void()> notify_;
};

// Strand-only. Payloads are domain structs; only the engine's ApiRouter turns them into API
// messages, so payload types must be copyable.
class EventBus {
public:
    explicit EventBus(EngineEpoch epoch) : epoch_(epoch) {}

    [[nodiscard]] EngineEpoch epoch() const noexcept { return epoch_; }

    template <class E>
    void publish(EventKind kind, E payload, EventScope scope = {}) {
        EventEnvelope event;
        event.kind = kind;
        event.session = scope.session;
        event.op = scope.op;
        event.coalesce_key = std::move(scope.coalesce_key);
        if constexpr (requires { payload.approx_bytes(); }) event.approx_bytes = payload.approx_bytes();
        else event.approx_bytes = sizeof(E);
        event.payload = std::move(payload);
        dispatch(std::move(event));
    }

    [[nodiscard]] std::shared_ptr<Subscription> subscribe(EventFilter filter, std::size_t byte_budget);

private:
    void dispatch(EventEnvelope event);
    void deliver(const EventEnvelope& event);

    EngineEpoch epoch_;
    u64 next_seq_ = 1;
    std::vector<std::weak_ptr<Subscription>> subscriptions_;
    // Events published from a notify callback wait here, so every queue receives seq in order.
    std::deque<EventEnvelope> deferred_;
    bool dispatching_ = false;
};

}  // namespace rb
