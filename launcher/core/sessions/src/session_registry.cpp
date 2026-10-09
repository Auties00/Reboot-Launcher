#include "reboot/sessions/session_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/flat_map.hpp"
#include "reboot/foundation/random.hpp"
#include "reboot/sessions/session_driver.hpp"
#include "reboot/sessions/session_event.hpp"
#include "reboot/sessions/sessions_error.hpp"

namespace rb::sessions {

namespace {

struct Session {
    SessionInfo info;
    std::unique_ptr<ISessionDriver> driver;
    Incarnation incarnation;
    bool respawn_pending = false;
    std::optional<StopRequest> stop;
    std::optional<i32> exit_code;
    std::size_t children_pending = 0;
    TimerHandle stop_deadline;
    std::vector<UniqueFunction<void()>> on_ended;
};

// Counts the sessions a stop_all still waits for.
struct StopBarrier {
    u64 id = 0;
    std::size_t remaining = 0;
    UniqueFunction<void()> done;
};

[[nodiscard]] std::unexpected<Diagnostic> fail(SessionsErrorCode code, SessionId session) {
    return std::unexpected(to_diagnostic(SessionsError{.code = code, .session = session, .from = {}, .to = {}}));
}

}  // namespace

struct SessionRegistry::State {
    IClock& clock;
    IRandom& random;
    Executor& strand;
    TimerService& timers;
    EventBus& events;
    // In open order, so parents come before their children.
    std::vector<std::unique_ptr<Session>> sessions;
    FlatSet<SessionId> ended;
    std::vector<StopBarrier> barriers;
    u64 next_barrier = 1;
    bool refusing_new = false;
    // Cancelled by the destructor, so a task posted earlier finds no state to touch.
    CancelSource alive;

    void post(UniqueFunction<void()> task) {
        strand.post([alive = alive.token(), task = std::move(task)]() mutable {
            if (!alive.cancelled()) task();
        });
    }

    [[nodiscard]] Session* find(SessionId id) const {
        const auto it = std::ranges::find(sessions, id, [](const auto& session) { return session->info.id; });
        return it == sessions.end() ? nullptr : it->get();
    }

    [[nodiscard]] Result<Session*> lookup(SessionId id) const {
        if (Session* session = find(id)) return session;
        return fail(ended.contains(id) ? SessionsErrorCode::Ended : SessionsErrorCode::NotFound, id);
    }

    void publish_state(const Session& session) {
        const SessionInfo& info = session.info;
        events.publish(EventKind::SessionStateChanged,
                       SessionStateChanged{.session = info.id,
                                           .kind = info.kind,
                                           .phase = info.phase,
                                           .lease = info.lease,
                                           .parent = info.parent,
                                           .label = info.label,
                                           .started_at = info.started_at},
                       EventScope{.session = info.id, .op = {}, .coalesce_key = {}});
    }

    void post_all(std::vector<UniqueFunction<void()>> callbacks) {
        for (auto& callback : callbacks) strand.post(std::move(callback));
    }

    void stop(Session& session, StopRequest request, UniqueFunction<void()> on_ended) {
        if (on_ended) session.on_ended.push_back(std::move(on_ended));
        if (session.stop) return;
        session.info.phase = SessionPhase::Stopping;
        session.info.stop_reason = request.reason;
        const auto grace = request.grace;
        session.stop = std::move(request);
        publish_state(session);

        const SessionId id = session.info.id;
        // A copy: a child's driver runs inside this loop and may reach the registry.
        const std::vector<SessionId> children = session.info.children;
        for (const SessionId child_id : children) {
            Session* child = find(child_id);
            if (child == nullptr) continue;
            ++session.children_pending;
            stop(*child, StopRequest{.reason = StopReason::ParentEnded, .grace = grace, .error = {}},
                 [this, alive = alive.token(), id] {
                     if (!alive.cancelled()) on_child_ended(id);
                 });
        }
        if (session.children_pending == 0) stop_driver(session);
    }

    void on_child_ended(SessionId parent) {
        Session* session = find(parent);
        if (session == nullptr || --session->children_pending > 0) return;
        stop_driver(*session);
    }

    void stop_driver(Session& session) {
        const SessionId id = session.info.id;
        session.stop_deadline = timers.after(session.stop->grace + kStopKillMargin, [this, id] {
            end(id, to_diagnostic(SessionsError{
                        .code = SessionsErrorCode::StopOverran, .session = id, .from = {}, .to = {}}));
        });
        // Posted, so the driver is never destroyed inside its own stop(); a `done` after the registry is a no-op.
        session.driver->stop(*session.stop, [this, alive = alive.token(), id](Result<void> stopped) {
            if (alive.cancelled()) return;
            post([this, id, stopped = std::move(stopped)]() mutable {
                end(id, stopped ? std::optional<Diagnostic>() : std::optional<Diagnostic>(std::move(stopped.error())));
            });
        });
    }

    // A late driver `done` after the deadline finds the session gone and changes nothing.
    void end(SessionId id, std::optional<Diagnostic> failure) {
        const auto it = std::ranges::find(sessions, id, [](const auto& session) { return session->info.id; });
        if (it == sessions.end()) return;
        std::unique_ptr<Session> session = std::move(*it);
        sessions.erase(it);
        ended.insert(id);
        session->stop_deadline.cancel();

        const SessionInfo& info = session->info;
        if (info.parent)
            if (Session* parent = find(*info.parent)) std::erase(parent->info.children, id);

        std::optional<Diagnostic> error = std::move(session->stop->error);
        if (failure && error) failure->causes.push_back(std::move(*error));
        if (failure) error = std::move(failure);
        // Before SessionEnded, so whatever the driver held is free when listeners and `on_ended` run.
        session->driver.reset();

        // Erased first, so a listener woken by SessionEnded already sees has_live() without it.
        events.publish(EventKind::SessionEnded,
                       SessionEnded{.session = id,
                                    .kind = info.kind,
                                    .parent = info.parent,
                                    .reason = session->stop->reason,
                                    .exit_code = session->exit_code,
                                    .error = std::move(error)},
                       EventScope{.session = id, .op = {}, .coalesce_key = {}});
        post_all(std::move(session->on_ended));
    }

    [[nodiscard]] UniqueFunction<void()> join_barrier(u64 barrier) {
        return [this, alive = alive.token(), barrier] {
            if (alive.cancelled()) return;
            const auto it = std::ranges::find(barriers, barrier, &StopBarrier::id);
            if (it == barriers.end() || --it->remaining > 0) return;
            UniqueFunction<void()> done = std::move(it->done);
            barriers.erase(it);
            done();
        };
    }

    void stop_each(const std::vector<SessionId>& ids, const StopRequest& request, UniqueFunction<void()> on_ended) {
        if (ids.empty()) {
            strand.post(std::move(on_ended));
            return;
        }
        const u64 barrier = next_barrier++;
        barriers.push_back(StopBarrier{.id = barrier, .remaining = ids.size(), .done = std::move(on_ended)});
        for (const SessionId id : ids) {
            if (Session* session = find(id)) stop(*session, request, join_barrier(barrier));
            else strand.post(join_barrier(barrier));
        }
    }
};

SessionRegistry::SessionRegistry(IClock& clock, IRandom& random, Executor& strand, TimerService& timers,
                                 EventBus& events)
    : state_(std::make_unique<State>(State{.clock = clock,
                                           .random = random,
                                           .strand = strand,
                                           .timers = timers,
                                           .events = events,
                                           .sessions = {},
                                           .ended = {},
                                           .barriers = {},
                                           .next_barrier = 1,
                                           .refusing_new = false,
                                           .alive = {}})) {}

// Drivers go children first, while the state is still whole, since a driver's destructor may call back in.
SessionRegistry::~SessionRegistry() {
    state_->alive.cancel(CancelReason::Shutdown);
    std::vector<std::unique_ptr<Session>> sessions = std::move(state_->sessions);
    state_->sessions.clear();
    while (!sessions.empty()) sessions.pop_back();
}

Result<SessionId> SessionRegistry::open(SessionSpec spec, std::unique_ptr<ISessionDriver> driver) {
    if (!driver) return std::unexpected(internal_bug("session_registry.open"));
    if (state_->refusing_new)
        return std::unexpected(to_diagnostic(SessionsError{.code = SessionsErrorCode::RefusingNew}));
    Session* parent = nullptr;
    if (spec.parent) {
        parent = state_->find(*spec.parent);
        if (parent == nullptr || parent->info.kind != SessionKind::Play || !is_active(parent->info.phase))
            return fail(SessionsErrorCode::ParentNotLive, *spec.parent);
    }

    auto session = std::make_unique<Session>();
    SessionInfo& info = session->info;
    info.id = SessionId{uuid_v4(state_->random)};
    info.kind = spec.kind;
    info.lease = spec.lease;
    info.pinned = std::move(spec.pinned);
    info.parent = spec.parent;
    info.label = std::move(spec.label);
    info.version = spec.version;
    info.runner = spec.runner;
    info.profile = spec.profile;
    info.started_at = state_->clock.system_now();
    session->driver = std::move(driver);

    const SessionId id = info.id;
    if (parent != nullptr) parent->info.children.push_back(id);
    state_->sessions.push_back(std::move(session));
    state_->publish_state(*state_->sessions.back());
    return id;
}

Result<void> SessionRegistry::set_phase(SessionId id, SessionPhase phase) {
    auto session = state_->lookup(id);
    if (!session) return std::unexpected(std::move(session.error()));
    SessionInfo& info = (*session)->info;
    if (!is_active(info.phase) || !is_active(phase))
        return std::unexpected(to_diagnostic(SessionsError{
            .code = SessionsErrorCode::InvalidTransition, .session = id, .from = info.phase, .to = phase}));
    if (info.phase == phase) return {};
    info.phase = phase;
    state_->publish_state(**session);
    return {};
}

Result<void> SessionRegistry::set_lease(SessionId id, Lease lease) {
    auto session = state_->lookup(id);
    if (!session) return std::unexpected(std::move(session.error()));
    if ((*session)->info.lease == lease) return {};
    (*session)->info.lease = lease;
    state_->publish_state(**session);
    return {};
}

Result<Incarnation> SessionRegistry::note_spawned(SessionId id, SpawnedProcess process) {
    auto found = state_->lookup(id);
    if (!found) return std::unexpected(std::move(found.error()));
    Session& session = **found;
    if (is_primary(process.role)) {
        if (session.respawn_pending) session.respawn_pending = false;
        else ++session.incarnation.value;
    }
    session.info.processes.push_back(process);
    state_->events.publish(EventKind::SessionSpawned, SessionSpawned{.session = id, .process = process},
                           EventScope{.session = id, .op = {}, .coalesce_key = {}});
    return session.incarnation;
}

Result<Incarnation> SessionRegistry::begin_respawn(SessionId id) {
    auto found = state_->lookup(id);
    if (!found) return std::unexpected(std::move(found.error()));
    Session& session = **found;
    if (!is_active(session.info.phase)) return fail(SessionsErrorCode::Stopping, id);
    if (!session.respawn_pending) ++session.incarnation.value;
    session.respawn_pending = true;
    return session.incarnation;
}

void SessionRegistry::note_process_exited(SessionId id, SpawnedProcess process) {
    Session* session = state_->find(id);
    if (session == nullptr || std::erase(session->info.processes, process) == 0) return;
    state_->publish_state(*session);
}

Result<void> SessionRegistry::raise_degraded(SessionId id, Diagnostic condition) {
    auto found = state_->lookup(id);
    if (!found) return std::unexpected(std::move(found.error()));
    std::vector<Diagnostic>& degraded = (*found)->info.degraded;
    std::erase_if(degraded, [&](const Diagnostic& raised) { return raised.id == condition.id; });
    degraded.push_back(condition);
    state_->events.publish(EventKind::SessionDegraded,
                           SessionDegraded{.session = id, .condition = std::move(condition)},
                           EventScope{.session = id, .op = {}, .coalesce_key = {}});
    return {};
}

Result<void> SessionRegistry::clear_degraded(SessionId id, std::string_view message_id) {
    auto found = state_->lookup(id);
    if (!found) return std::unexpected(std::move(found.error()));
    if (std::erase_if((*found)->info.degraded, [&](const Diagnostic& raised) { return raised.id == message_id; }) > 0)
        state_->publish_state(**found);
    return {};
}

void SessionRegistry::report_exit(SessionId id, Incarnation incarnation, SessionExit exit) {
    Session* session = state_->find(id);
    if (session == nullptr || incarnation != session->incarnation) return;
    if (!session->exit_code) session->exit_code = exit.exit_code;
    if (session->stop) return;
    state_->stop(*session,
                 StopRequest{.reason = stop_reason_for(exit.reason),
                             .grace = default_deadline(OpKind::GracefulStop),
                             .error = std::move(exit.error)},
                 nullptr);
}

Result<void> SessionRegistry::stop(SessionId id, StopRequest request, UniqueFunction<void()> on_ended) {
    if (Session* session = state_->find(id)) {
        state_->stop(*session, std::move(request), std::move(on_ended));
        return {};
    }
    if (!state_->ended.contains(id)) return fail(SessionsErrorCode::NotFound, id);
    if (on_ended) state_->strand.post(std::move(on_ended));
    return {};
}

void SessionRegistry::stop_all(std::optional<SessionKind> kind, StopRequest request, UniqueFunction<void()> on_ended) {
    std::vector<SessionId> ids;
    for (const auto& session : state_->sessions)
        if (!kind || session->info.kind == *kind) ids.push_back(session->info.id);
    state_->stop_each(ids, request, std::move(on_ended));
}

void SessionRegistry::on_connection_closed(ConnectionId connection) {
    std::vector<SessionId> ids;
    for (const auto& session : state_->sessions)
        if (session->info.lease.client == connection) ids.push_back(session->info.id);
    for (const SessionId id : ids)
        if (Session* session = state_->find(id))
            state_->stop(*session, StopRequest{.reason = StopReason::LeaseEnded}, nullptr);
}

void SessionRegistry::refuse_new() noexcept { state_->refusing_new = true; }

bool SessionRegistry::has_live() const noexcept { return !state_->sessions.empty(); }

std::vector<SessionInfo> SessionRegistry::list() const {
    std::vector<SessionInfo> out;
    out.reserve(state_->sessions.size());
    for (const auto& session : state_->sessions) out.push_back(session->info);
    return out;
}

Result<SessionInfo> SessionRegistry::get(SessionId id) const {
    auto found = state_->lookup(id);
    if (!found) return std::unexpected(std::move(found.error()));
    return (*found)->info;
}

std::vector<SessionId> SessionRegistry::sessions_using(BuildId build) const {
    std::vector<SessionId> out;
    for (const auto& session : state_->sessions)
        if (session->info.pinned.build == build) out.push_back(session->info.id);
    return out;
}

void SessionRegistry::stop_sessions_using(BuildId build, UniqueFunction<void(Result<void>)> done) {
    state_->stop_each(sessions_using(build), StopRequest{.reason = StopReason::BuildRemoved},
                      [done = std::move(done)]() mutable { done(Result<void>{}); });
}

}  // namespace rb::sessions
