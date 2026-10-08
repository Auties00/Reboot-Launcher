#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {

class IClock;
class TimerService;
class EventBus;

enum class OpKind : u8 {
    HttpSmall,
    HttpDownload,
    Dns,
    QuicConnect,
    BackendReady,
    ChildHello,
    GameControlHello,
    HostReadiness,
    GracefulStop,
    UpnpDiscover,
    UpnpMap,
    Wmi,
    EngineConnect,
    ShutdownBudget,
    Install,
    Import,
    Play,
    Host,
    ComponentEnsure,
    RuntimeSetup,
    UpdateCheck,
    UpdateApply,
    LogExport,
    Generic,
};

// Long-running kinds use a liveness bound instead: no progress for this long.
[[nodiscard]] constexpr bool uses_liveness_deadline(OpKind kind) noexcept {
    return kind >= OpKind::Install;
}

// HttpDownload's value is its stall window; the transport enforces the 10 s connect bound and
// the 1 KiB/s floor itself.
[[nodiscard]] constexpr std::chrono::milliseconds default_deadline(OpKind kind) noexcept {
    using std::chrono::seconds;
    switch (kind) {
        case OpKind::HttpSmall: return seconds{20};
        case OpKind::HttpDownload: return seconds{30};
        case OpKind::Dns: return seconds{5};
        case OpKind::QuicConnect: return seconds{10};
        case OpKind::BackendReady: return seconds{30};
        case OpKind::ChildHello: return seconds{10};
        case OpKind::GameControlHello: return seconds{2};
        case OpKind::HostReadiness: return seconds{120};
        case OpKind::GracefulStop: return seconds{5};
        case OpKind::UpnpDiscover: return seconds{3};
        case OpKind::UpnpMap: return seconds{5};
        case OpKind::Wmi: return seconds{5};
        case OpKind::EngineConnect: return seconds{10};
        case OpKind::ShutdownBudget: return seconds{30};
        default: return seconds{120};
    }
}

enum class RunnerMultiplier : u8 { Native = 1, Wine = 2, RosettaFirstRun = 4 };

[[nodiscard]] constexpr std::chrono::milliseconds scaled(std::chrono::milliseconds deadline,
                                                         RunnerMultiplier multiplier) noexcept {
    return deadline * static_cast<int>(multiplier);
}

// `phase` must be a string literal: progress is coalesced and published later.
struct Progress {
    std::string_view phase;
    u64 done = 0;
    std::optional<u64> total;
    std::optional<u64> rate_per_s;
    std::optional<std::chrono::seconds> eta;
};

template <class T>
struct Completed {
    T value;
};
template <>
struct Completed<void> {};

struct Failed {
    Diagnostic error;
};
struct Cancelled {
    CancelReason reason{};
};
struct TimedOut {
    std::string phase;
};

template <class T>
using Outcome = std::variant<Completed<T>, Failed, Cancelled, TimedOut>;

// Completed<void> erases to an empty std::any.
using ErasedOutcome = Outcome<std::any>;

enum class DisconnectPolicy : u8 { BoundToConnection, Detached };

using ConnectionId = Counter<struct ConnTag, u64>;

struct OpProgressEvent {
    OpId op;
    OpKind kind{};
    std::string phase;
    u64 done = 0;
    std::optional<u64> total;
    std::optional<u64> rate_per_s;
    std::optional<std::chrono::seconds> eta;
    std::optional<RequestId> awaiting_user;
};

struct OpCompletedEvent {
    OpId op;
    OpKind kind{};
    ErasedOutcome outcome;
};

// An op with no outcome yet.
struct LiveOp {
    OpId op;
    OpKind kind{};
    DisconnectPolicy policy{};
    std::optional<SessionId> session;
    // Absent before the op's first progress.
    std::optional<OpProgressEvent> progress;
};

class OpHandle {
public:
    OpHandle() = default;
    explicit OpHandle(OpId id) : id_(id) {}

    [[nodiscard]] OpId id() const noexcept { return id_; }

private:
    OpId id_;
};

class OpRegistry;

// Completes exactly once: the first complete() moves the state Pending -> Completing -> Done.
class OperationBase {
public:
    OperationBase(const OperationBase&) = delete;
    OperationBase& operator=(const OperationBase&) = delete;
    virtual ~OperationBase();

    [[nodiscard]] OpId id() const noexcept { return id_; }
    [[nodiscard]] OpKind kind() const noexcept { return kind_; }
    [[nodiscard]] CancelToken token() const { return cancel_.token(); }
    [[nodiscard]] bool done() const noexcept { return state_.load(std::memory_order_acquire) == State::Done; }

    // Coalesced to 10 Hz; for liveness kinds it also re-arms the deadline.
    void progress(const Progress& progress);
    // Suspends the deadline until the next progress(); cancellation still applies.
    void awaiting_user(RequestId request);

protected:
    OperationBase(OpRegistry& registry, OpId id, OpKind kind) : registry_(registry), id_(id), kind_(kind) {}

    bool complete_erased(ErasedOutcome outcome);

private:
    friend class OpRegistry;
    enum class State : u8 { Pending, Completing, Done };

    OpRegistry& registry_;
    OpId id_;
    OpKind kind_;
    std::atomic<State> state_{State::Pending};
    CancelSource cancel_;
};

template <class T>
class Operation final : public OperationBase {
public:
    // False when the operation already has an outcome.
    bool complete(Outcome<T> outcome) { return complete_erased(erase(std::move(outcome))); }

private:
    friend class OpRegistry;
    Operation(OpRegistry& registry, OpId id, OpKind kind) : OperationBase(registry, id, kind) {}

    static ErasedOutcome erase(Outcome<T>&& outcome) {
        return std::visit(
            []<class A>(A&& alternative) -> ErasedOutcome {
                using V = std::remove_cvref_t<A>;
                if constexpr (!std::is_same_v<V, Completed<T>>) {
                    return std::forward<A>(alternative);
                } else if constexpr (std::is_void_v<T>) {
                    return Completed<std::any>{};
                } else {
                    return Completed<std::any>{std::any(std::move(alternative.value))};
                }
            },
            std::move(outcome));
    }
};

// Strand-only. Keeps a terminal outcome until every attached connection released it, or for
// 10 minutes after it completed.
class OpRegistry {
public:
    OpRegistry(IClock& clock, TimerService& timers, EventBus& events);
    ~OpRegistry();
    OpRegistry(const OpRegistry&) = delete;
    OpRegistry& operator=(const OpRegistry&) = delete;

    template <class T>
    std::pair<OpHandle, Operation<T>&> create(OpKind kind, DisconnectPolicy policy, std::optional<SessionId> session,
                                              RunnerMultiplier multiplier = RunnerMultiplier::Native,
                                              std::optional<std::chrono::milliseconds> deadline = std::nullopt) {
        const OpId id = next_id();
        std::unique_ptr<Operation<T>> op(new Operation<T>(*this, id, kind));
        Operation<T>& ref = *op;
        adopt(std::move(op), policy, session, scaled(deadline.value_or(default_deadline(kind)), multiplier));
        return {OpHandle(id), ref};
    }

    Result<void> attach(OpId op, ConnectionId connection);
    void release(OpId op, ConnectionId connection);
    // Idempotent; cancelling a finished op succeeds and changes nothing.
    Result<void> cancel(OpId op, CancelReason reason);
    // Cancels this connection's BoundToConnection ops and drops its attachments.
    void on_connection_closed(ConnectionId connection);

    [[nodiscard]] std::optional<ErasedOutcome> outcome(OpId op) const;
    [[nodiscard]] bool has_live_detached() const;
    // By id.
    [[nodiscard]] std::vector<LiveOp> live() const;

private:
    friend class OperationBase;

    OpId next_id();
    void adopt(std::unique_ptr<OperationBase> op, DisconnectPolicy policy, std::optional<SessionId> session,
               std::chrono::milliseconds deadline);
    void on_progress(OperationBase& op, const Progress& progress);
    void on_awaiting_user(OperationBase& op, RequestId request);
    void on_completed(OperationBase& op, ErasedOutcome outcome);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot
