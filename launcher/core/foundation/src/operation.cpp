#include "reboot/foundation/operation.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"

namespace reboot {

namespace {

constexpr std::chrono::milliseconds kProgressInterval{100};
constexpr std::chrono::minutes kRetention{10};

Diagnostic op_not_found(OpId op) {
    return make_diag(ErrorDomain::Foundation, msg::kOpNotFound).kind(ErrorKind::NotFound).arg("op", op.value).build();
}

}  // namespace

struct OpRegistry::Impl {
    struct Record {
        std::unique_ptr<OperationBase> op;
        DisconnectPolicy policy{};
        std::optional<SessionId> session;
        std::chrono::milliseconds deadline{};
        // For total deadlines: what is left of the budget while AwaitingUser suspends it.
        std::chrono::steady_clock::duration remaining{};
        SteadyTime deadline_at{};
        TimerHandle deadline_timer;
        bool awaiting_user = false;

        OpProgressEvent last_progress;
        std::optional<SteadyTime> last_published;
        bool progress_pending = false;
        TimerHandle progress_timer;

        std::set<ConnectionId> attached;
        std::optional<ErasedOutcome> outcome;
        TimerHandle retention_timer;
    };

    Impl(IClock& clock_ref, TimerService& timer_service, EventBus& bus)
        : clock(clock_ref), timers(timer_service), events(bus) {}

    Record* find(OpId id) {
        const auto it = records.find(id);
        return it == records.end() ? nullptr : &it->second;
    }

    [[nodiscard]] EventScope scope_of(const Record& record) const {
        return EventScope{record.session, record.op->id(), std::to_string(record.op->id().value)};
    }

    void arm_deadline(Record& record, std::chrono::steady_clock::duration budget) {
        record.deadline_at = clock.steady_now() + budget;
        record.deadline_timer = timers.at(record.deadline_at, [this, id = record.op->id()] { on_deadline(id); });
    }

    void on_deadline(OpId id) {
        Record* record = find(id);
        if (record == nullptr || record->awaiting_user || record->op->done()) return;
        time_out(*record->op, record->last_progress.phase);
    }

    void publish_progress(Record& record) {
        record.progress_pending = false;
        record.progress_timer.cancel();
        record.last_published = clock.steady_now();
        events.publish(EventKind::OpProgress, record.last_progress, scope_of(record));
    }

    void erase_if_released(OpId id) {
        const auto it = records.find(id);
        if (it == records.end() || !it->second.outcome || !it->second.attached.empty()) return;
        // Destroyed after the erase: its timers and the operation must not see a half-erased map.
        Record removed = std::move(it->second);
        records.erase(it);
    }

    IClock& clock;
    TimerService& timers;
    EventBus& events;
    // Set by OpRegistry, whose friendship with OperationBase a nested type does not portably share.
    UniqueFunction<void(OperationBase&, std::string)> time_out;
    u64 next_id = 1;
    std::map<OpId, Record> records;
};

OperationBase::~OperationBase() = default;

void OperationBase::progress(const Progress& progress) {
    if (state_.load(std::memory_order_acquire) != State::Pending) return;
    registry_.on_progress(*this, progress);
}

void OperationBase::awaiting_user(RequestId request) {
    if (state_.load(std::memory_order_acquire) != State::Pending) return;
    registry_.on_awaiting_user(*this, request);
}

bool OperationBase::complete_erased(ErasedOutcome outcome) {
    State expected = State::Pending;
    if (!state_.compare_exchange_strong(expected, State::Completing, std::memory_order_acq_rel)) return false;
    registry_.on_completed(*this, std::move(outcome));
    state_.store(State::Done, std::memory_order_release);
    return true;
}

OpRegistry::OpRegistry(IClock& clock, TimerService& timers, EventBus& events)
    : impl_(std::make_unique<Impl>(clock, timers, events)) {
    impl_->time_out = [](OperationBase& op, std::string phase) {
        op.complete_erased(TimedOut{std::move(phase)});
        op.cancel_.cancel(CancelReason::Deadline);
    };
}

OpRegistry::~OpRegistry() = default;

OpId OpRegistry::next_id() { return OpId{impl_->next_id++}; }

void OpRegistry::adopt(std::unique_ptr<OperationBase> op, DisconnectPolicy policy, std::optional<SessionId> session,
                       std::chrono::milliseconds deadline) {
    const OpId id = op->id();
    Impl::Record& record = impl_->records[id];
    record.last_progress.op = id;
    record.last_progress.kind = op->kind();
    record.op = std::move(op);
    record.policy = policy;
    record.session = session;
    record.deadline = deadline;
    impl_->arm_deadline(record, deadline);
}

Result<void> OpRegistry::attach(OpId op, ConnectionId connection) {
    Impl::Record* record = impl_->find(op);
    if (record == nullptr) return std::unexpected(op_not_found(op));
    record->attached.insert(connection);
    return {};
}

void OpRegistry::release(OpId op, ConnectionId connection) {
    Impl::Record* record = impl_->find(op);
    if (record == nullptr) return;
    record->attached.erase(connection);
    impl_->erase_if_released(op);
}

Result<void> OpRegistry::cancel(OpId op, CancelReason reason) {
    Impl::Record* record = impl_->find(op);
    if (record == nullptr) return std::unexpected(op_not_found(op));
    OperationBase& operation = *record->op;
    if (operation.done()) return {};
    // The outcome is decided before the work hears of it, so the reason it sees is final.
    operation.complete_erased(Cancelled{reason});
    operation.cancel_.cancel(reason);
    return {};
}

void OpRegistry::on_connection_closed(ConnectionId connection) {
    std::vector<OpId> affected;
    for (const auto& [id, record] : impl_->records)
        if (record.attached.contains(connection)) affected.push_back(id);
    for (const OpId id : affected) {
        Impl::Record* record = impl_->find(id);
        if (record == nullptr) continue;
        record->attached.erase(connection);
        if (record->policy == DisconnectPolicy::BoundToConnection) (void)cancel(id, CancelReason::Disconnect);
        impl_->erase_if_released(id);
    }
}

std::optional<ErasedOutcome> OpRegistry::outcome(OpId op) const {
    const auto it = impl_->records.find(op);
    if (it == impl_->records.end()) return std::nullopt;
    return it->second.outcome;
}

bool OpRegistry::has_live_detached() const {
    for (const auto& entry : impl_->records)
        if (entry.second.policy == DisconnectPolicy::Detached && !entry.second.outcome) return true;
    return false;
}

std::vector<LiveOp> OpRegistry::live() const {
    std::vector<LiveOp> ops;
    for (const auto& [id, record] : impl_->records) {
        if (record.outcome) continue;
        LiveOp& op = ops.emplace_back(LiveOp{id, record.op->kind(), record.policy, record.session, std::nullopt});
        if (record.last_published || record.progress_pending) op.progress = record.last_progress;
    }
    return ops;
}

void OpRegistry::on_progress(OperationBase& op, const Progress& progress) {
    Impl::Record* record = impl_->find(op.id());
    if (record == nullptr) return;

    if (record->awaiting_user) {
        record->awaiting_user = false;
        impl_->arm_deadline(*record, uses_liveness_deadline(op.kind()) ? record->deadline : record->remaining);
    } else if (uses_liveness_deadline(op.kind())) {
        impl_->arm_deadline(*record, record->deadline);
    }

    OpProgressEvent& event = record->last_progress;
    event.phase = std::string(progress.phase);
    event.done = progress.done;
    event.total = progress.total;
    event.rate_per_s = progress.rate_per_s;
    event.eta = progress.eta;
    event.awaiting_user.reset();

    const SteadyTime now = impl_->clock.steady_now();
    if (!record->last_published || now - *record->last_published >= kProgressInterval) {
        impl_->publish_progress(*record);
        return;
    }
    if (record->progress_pending) return;
    record->progress_pending = true;
    record->progress_timer = impl_->timers.at(*record->last_published + kProgressInterval, [impl = impl_.get(), id = op.id()] {
        if (Impl::Record* pending = impl->find(id); pending != nullptr && pending->progress_pending)
            impl->publish_progress(*pending);
    });
}

void OpRegistry::on_awaiting_user(OperationBase& op, RequestId request) {
    Impl::Record* record = impl_->find(op.id());
    if (record == nullptr) return;
    if (!record->awaiting_user) {
        record->awaiting_user = true;
        const auto left = record->deadline_at - impl_->clock.steady_now();
        record->remaining = left > std::chrono::steady_clock::duration::zero() ? left : std::chrono::steady_clock::duration::zero();
        record->deadline_timer.cancel();
    }
    record->last_progress.awaiting_user = request;
    impl_->publish_progress(*record);
}

void OpRegistry::on_completed(OperationBase& op, ErasedOutcome outcome) {
    Impl::Record* record = impl_->find(op.id());
    if (record == nullptr) return;
    record->deadline_timer.cancel();
    record->progress_timer.cancel();
    record->progress_pending = false;
    record->outcome = outcome;
    record->retention_timer =
        impl_->timers.after(kRetention, [impl = impl_.get(), id = op.id()] {
            const auto it = impl->records.find(id);
            if (it == impl->records.end()) return;
            Impl::Record removed = std::move(it->second);
            impl->records.erase(it);
        });
    impl_->events.publish(EventKind::OpCompleted, OpCompletedEvent{op.id(), op.kind(), std::move(outcome)},
                          EventScope{record->session, op.id(), {}});
}

}  // namespace reboot
