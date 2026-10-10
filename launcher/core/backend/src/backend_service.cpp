#include "reboot/backend/backend_service.hpp"

#include <algorithm>
#include <any>
#include <chrono>
#include <deque>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "messages.hpp"
#include "reboot/backend/backend_event.hpp"
#include "reboot/backend/backend_process.hpp"
#include "reboot/backend/backend_process_observer.hpp"
#include "reboot/backend/backend_sessions.hpp"
#include "reboot/backend/remote_backend_probe.hpp"
#include "reboot/backend/unencrypted_upstream_prompt.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/net/host_tls_memory.hpp"
#include "reboot/process/restart_policy.hpp"

namespace rb::backend {

namespace {

constexpr std::string_view kLoopbackHost = "127.0.0.1";
constexpr std::string_view kRestartLimitId = "process.child_restart_limit";
constexpr std::string_view kNeedsConsentId = "net.plain_http_needs_consent";

void log(LogLevel level, std::string text) {
    if (Logger::enabled(level)) Logger::write(level, LogCategory::Backend, std::nullopt, std::move(text));
}

[[nodiscard]] DiagBuilder backend_diag(MessageId message) { return make_diag(ErrorDomain::Backend, message); }

[[nodiscard]] Diagnostic not_ready() {
    return backend_diag(msg::kNotReady)
        .arg("seconds", std::chrono::duration_cast<std::chrono::seconds>(default_deadline(OpKind::BackendReady)).count())
        .retryable();
}

[[nodiscard]] BackendUpstream embedded_upstream(u16 http_port) {
    return BackendUpstream{.origin = std::format("http://{}:{}", kLoopbackHost, http_port),
                           .flavor = identity::UpstreamFlavor::Reboot,
                           .websocket = HostPort{std::string(kLoopbackHost), Port{http_port}},
                           .http_port = http_port};
}

[[nodiscard]] BackendUpstream remote_upstream(const BackendConfig& config, const BackendInfo& info) {
    BackendUpstream upstream{.origin = info.url.origin(), .flavor = info.flavor, .websocket = std::nullopt,
                             .http_port = std::nullopt};
    if (std::optional<HostPort> xmpp = config.target.xmpp()) {
        if (!xmpp->port) xmpp->port = storage::kDefaultXmppPort;
        upstream.websocket = std::move(*xmpp);
    } else if (info.flavor == identity::UpstreamFlavor::Reboot && info.ws_port) {
        upstream.websocket = HostPort{info.url.host, *info.ws_port};
    }
    return upstream;
}

// What an unrequested exit leaves in last_error: the supervisor's verdict as the cause.
[[nodiscard]] Diagnostic crash_error(const process::ChildExitInfo& exit) {
    const process::RestartPolicy policy;
    DiagBuilder builder = exit.error && exit.error->id == kRestartLimitId
                              ? backend_diag(msg::kRestartLimitReached)
                                    .arg("restarts", policy.max_restarts)
                                    .arg("minutes", std::chrono::duration_cast<std::chrono::minutes>(policy.window).count())
                              : backend_diag(msg::kCrashed);
    if (exit.error) std::move(builder).cause(*exit.error);
    return std::move(builder).build();
}

}  // namespace

struct BackendService::Impl final : IBackendProcessObserver {
    enum class CommandKind : u8 { Start, Stop };

    struct Command {
        CommandKind kind = CommandKind::Start;
        BackendStopCause cause = BackendStopCause::UserStop;
        std::vector<Operation<BackendUpstream>*> start_ops;
        std::vector<Operation<void>*> stop_ops;
        std::vector<UniqueFunction<void(Result<void>)>> stop_dones;
    };

    struct Waiter {
        u64 lease = 0;
        UniqueFunction<void(Result<BackendUpstream>)> done;
        TimerHandle deadline;
        CancelRegistration cancel;
    };

    struct LeaseRecord {
        std::optional<SessionId> session;
        bool embedded = false;
    };

    Impl(BackendConfig config, BackendProcess& process_ref, RemoteBackendProbe& probe_ref, IBackendSessions& sessions_ref,
         OpRegistry& ops_ref, UserRequestRegistry& requests_ref, net::HostTlsMemory& tls_ref, EventBus& events_ref,
         Executor& strand_ref, TimerService& timers_ref)
        : process(process_ref),
          probe(probe_ref),
          sessions(sessions_ref),
          ops(ops_ref),
          requests(requests_ref),
          tls(tls_ref),
          events(events_ref),
          strand(strand_ref),
          timers(timers_ref) {
        state.config = std::move(config);
    }

    // --- events and small helpers

    void publish(BackendChange change, std::optional<BackendStopCause> cause = std::nullopt) {
        events.publish(EventKind::BackendStateChanged, BackendEvent{change, state, cause});
    }

    void post(UniqueFunction<void()> task) {
        strand.post([alive = alive.token(), task = std::move(task)]() mutable {
            if (!alive.cancelled()) task();
        });
    }

    template <class T>
    void answer_later(UniqueFunction<void(Result<T>)> done, Result<T> result) {
        post([done = std::move(done), result = std::move(result)]() mutable { done(std::move(result)); });
    }

    [[nodiscard]] u32 session_leases() const noexcept {
        return static_cast<u32>(
            std::ranges::count_if(leases, [](const auto& entry) { return entry.second.session.has_value(); }));
    }

    [[nodiscard]] std::vector<SessionId> leased_sessions() const {
        std::vector<SessionId> out;
        for (const auto& [id, lease] : leases)
            if (lease.session) out.push_back(*lease.session);
        return out;
    }

    [[nodiscard]] Diagnostic in_use(std::size_t count) const {
        return backend_diag(msg::kInUse).arg("sessions", count).kind(ErrorKind::Conflict).build();
    }

    [[nodiscard]] Diagnostic shutting_down_error() const {
        return backend_diag(msg::kShuttingDown).kind(ErrorKind::EngineUnavailable).build();
    }

    [[nodiscard]] bool start_pending() const noexcept {
        if (current && current->kind == CommandKind::Start) return true;
        return std::ranges::any_of(queue, [](const Command& command) { return command.kind == CommandKind::Start; });
    }

    // Running, or on its way there.
    [[nodiscard]] bool heading_up() const noexcept {
        if (!queue.empty()) return queue.back().kind == CommandKind::Start;
        if (current) return current->kind == CommandKind::Start;
        return state.phase == BackendPhase::Running;
    }

    // --- the command queue

    void enqueue(Command command) {
        if (!current) {
            current = std::move(command);
            return execute();
        }
        if (command.kind == CommandKind::Start) {
            if (!queue.empty()) {
                if (queue.back().kind == CommandKind::Start) return merge(queue.back(), std::move(command));
                queue.push_back(std::move(command));
                return;
            }
            if (current->kind == CommandKind::Start) return merge(*current, std::move(command));
            queue.push_back(std::move(command));
            return;
        }
        // A start that has not run yet would only be undone by this stop.
        if (!queue.empty() && queue.back().kind == CommandKind::Start) {
            supersede(queue.back());
            queue.pop_back();
        }
        if (!queue.empty()) return merge(queue.back(), std::move(command));
        if (current->kind == CommandKind::Stop) return merge(*current, std::move(command));
        supersede(*current);
        current = std::move(command);
        execute();
    }

    static void merge(Command& into, Command from) {
        for (auto* op : from.start_ops) into.start_ops.push_back(op);
        for (auto* op : from.stop_ops) into.stop_ops.push_back(op);
        for (auto& done : from.stop_dones) into.stop_dones.push_back(std::move(done));
    }

    // The start's own ops end; waiters stay, since the next start may still serve them.
    void supersede(Command& start) {
        transition.cancel(CancelReason::Superseded);
        ready_timer.cancel();
        for (Operation<BackendUpstream>* op : std::exchange(start.start_ops, {}))
            op->complete(Cancelled{CancelReason::Superseded});
    }

    void execute() {
        transition = CancelSource{};
        ready_timed_out = false;
        if (current->kind == CommandKind::Start) run_start();
        else run_stop();
    }

    void next() {
        transition.cancel(CancelReason::Superseded);
        current.reset();
        if (queue.empty()) return;
        current = std::move(queue.front());
        queue.pop_front();
        execute();
    }

    // --- starting

    void run_start() {
        if (state.phase == BackendPhase::Running) return become_running(true);
        state.phase = BackendPhase::Starting;
        state.upstream.reset();
        publish(BackendChange::Starting);
        if (!state.config.target.embedded()) return probe_upstream();
        if (Result<void> started = process.start(state.config); !started) return fail_start(std::move(started.error()));
        arm_ready_deadline(default_deadline(OpKind::BackendReady));
    }

    void arm_ready_deadline(std::chrono::milliseconds budget) {
        ready_timer = timers.after(budget, [this] {
            if (!current || current->kind != CommandKind::Start) return;
            ready_timed_out = true;
            state.last_error = not_ready();
            if (!process.stop(std::chrono::milliseconds{0})) fail_start(not_ready());
        });
    }

    void probe_upstream() {
        const std::optional<BackendUrl> url = state.config.target.upstream_url();
        if (!url) return fail_start(internal_bug("backend.probe_without_upstream"));
        const CancelToken token = transition.token();
        probe.probe(*url, token, [this, alive_token = alive.token(), token](Result<BackendInfo> info) {
            if (alive_token.cancelled() || token.cancelled()) return;
            if (!info) {
                if (info.error().id == kNeedsConsentId) return ask_consent();
                return fail_start(std::move(info.error()));
            }
            state.version = info->version.value_or(std::string{});
            state.upstream = remote_upstream(state.config, *info);
            become_running(true);
        });
    }

    void ask_consent() {
        const std::optional<BackendUrl> url = state.config.target.upstream_url();
        BackendUrl plain = *url;
        plain.scheme = net::UrlScheme::Http;
        std::optional<OpId> op;
        if (!current->start_ops.empty()) op = current->start_ops.front()->id();
        const CancelToken token = transition.token();
        const RequestId id = requests.ask(
            UserRequestKind::ConfirmUnencryptedUpstream, UnencryptedUpstreamPrompt{plain.origin()}, op, std::nullopt,
            [this, alive_token = alive.token(), token, host = plain.host](const std::any& answer) -> Result<void> {
                const auto* decision = std::any_cast<UnencryptedUpstreamAnswer>(&answer);
                if (decision == nullptr) return backend_diag(msg::kAnswerInvalid).kind(ErrorKind::InvalidInput).fail();
                const bool accept = decision->accept;
                strand.post([this, alive_token, token, host, accept] {
                    if (alive_token.cancelled() || token.cancelled()) return;
                    if (!accept)
                        return fail_start(backend_diag(msg::kUnencryptedUpstreamDeclined)
                                              .arg("host", host)
                                              .kind(ErrorKind::Cancelled)
                                              .build());
                    tls.remember_http_acknowledged(host);
                    probe_upstream();
                });
                return {};
            },
            token);
        for (Operation<BackendUpstream>* start_op : current->start_ops) start_op->awaiting_user(id);
    }

    // `fresh`: the upstream was just checked, so it is handed out without asking again.
    void become_running(bool fresh) {
        ready_timer.cancel();
        const bool entered = state.phase != BackendPhase::Running;
        if (entered) {
            state.phase = BackendPhase::Running;
            state.last_error.reset();
            publish(BackendChange::Running);
        }
        std::vector<Operation<BackendUpstream>*> start_ops = std::exchange(current->start_ops, {});
        std::vector<std::unique_ptr<Waiter>> ready = take_waiters([](const Waiter&) { return true; });
        next();
        // After next(), so a listener that takes or drops a lease finds a settled queue.
        if (entered)
            for (auto& listener : ready_listeners) listener(state);
        for (Operation<BackendUpstream>* op : start_ops) complete_with_health(op, fresh);
        for (auto& waiter : ready) check_health(fresh, std::move(waiter->done));
    }

    void fail_start(Diagnostic error) {
        ready_timer.cancel();
        state.phase = BackendPhase::Failed;
        state.upstream.reset();
        state.last_error = error;
        publish(BackendChange::Failed);
        std::vector<Operation<BackendUpstream>*> start_ops = std::exchange(current->start_ops, {});
        std::vector<std::unique_ptr<Waiter>> failed = take_waiters([](const Waiter&) { return true; });
        next();
        for (Operation<BackendUpstream>* op : start_ops) op->complete(Failed{error});
        for (auto& waiter : failed) waiter->done(std::unexpected(error));
    }

    void complete_with_health(Operation<BackendUpstream>* op, bool fresh) {
        checking.push_back(op);
        check_health(fresh, [this, op](Result<BackendUpstream> upstream) {
            std::erase(checking, op);
            if (upstream) op->complete(Completed<BackendUpstream>{std::move(*upstream)});
            else op->complete(Failed{std::move(upstream.error())});
        });
    }

    void check_health(bool fresh, UniqueFunction<void(Result<BackendUpstream>)> done, CancelToken token = {}) {
        if (state.config.target.embedded()) {
            process.health([alive = alive.token(), done = std::move(done)](Result<BackendHealth> health) mutable {
                if (alive.cancelled()) return;
                if (!health) return done(std::unexpected(std::move(health.error())));
                done(embedded_upstream(health->http_port));
            });
            return;
        }
        if (fresh && state.upstream) return answer_later(std::move(done), Result<BackendUpstream>(*state.upstream));
        const std::optional<BackendUrl> url = state.config.target.upstream_url();
        if (!url)
            return answer_later(std::move(done),
                                Result<BackendUpstream>(std::unexpected(internal_bug("backend.probe_without_upstream"))));
        auto answer = [alive = alive.token(), config = state.config, done = std::move(done)](Result<BackendInfo> info) mutable {
            if (alive.cancelled()) return;
            if (!info) return done(std::unexpected(std::move(info.error())));
            done(remote_upstream(config, *info));
        };
        probe.probe(*url, std::move(token), std::move(answer));
    }

    // --- stopping

    void run_stop() {
        const BackendPhase before = state.phase;
        // Not gated on the target: a reconfigure away from Embedded has already replaced the config.
        if (process.stop()) {
            state.phase = BackendPhase::Stopping;
            state.upstream.reset();
            publish(BackendChange::Stopping, current->cause);
            return;
        }
        if (before == BackendPhase::Running || before == BackendPhase::Starting || before == BackendPhase::Restarting) {
            state.phase = BackendPhase::Stopping;
            publish(BackendChange::Stopping, current->cause);
        }
        finish_stop();
    }

    void finish_stop() {
        const BackendStopCause cause = current->cause;
        const bool changed = state.phase != BackendPhase::Stopped;
        state.phase = BackendPhase::Stopped;
        state.upstream.reset();
        if (changed) publish(BackendChange::Stopped, cause);
        std::vector<Operation<void>*> stop_ops = std::exchange(current->stop_ops, {});
        std::vector<UniqueFunction<void(Result<void>)>> dones = std::exchange(current->stop_dones, {});
        // Waiters stay for a start queued behind this stop.
        std::vector<std::unique_ptr<Waiter>> stranded;
        if (queue.empty() || queue.front().kind != CommandKind::Start)
            stranded = take_waiters([](const Waiter&) { return true; });
        next();
        for (Operation<void>* op : stop_ops) op->complete(Completed<void>{});
        for (auto& done : dones)
            if (done) done({});
        for (auto& waiter : stranded) {
            Diagnostic error = cause == BackendStopCause::Shutdown ? shutting_down_error()
                                                                   : backend_diag(msg::kStoppedBeforeReady).build();
            waiter->done(std::unexpected(std::move(error)));
        }
    }

    // --- waiters

    template <class Pred>
    std::vector<std::unique_ptr<Waiter>> take_waiters(Pred pred) {
        std::vector<std::unique_ptr<Waiter>> taken;
        for (auto it = waiters.begin(); it != waiters.end();) {
            if (pred(*it->second)) {
                taken.push_back(std::move(it->second));
                it = waiters.erase(it);
            } else {
                ++it;
            }
        }
        return taken;
    }

    void fail_waiter(u64 id, Diagnostic error) {
        const auto found = waiters.find(id);
        if (found == waiters.end()) return;
        std::unique_ptr<Waiter> waiter = std::move(found->second);
        waiters.erase(found);
        waiter->done(std::unexpected(std::move(error)));
    }

    void add_waiter(u64 lease, CancelToken token, UniqueFunction<void(Result<BackendUpstream>)> done) {
        const u64 id = next_waiter++;
        auto waiter = std::make_unique<Waiter>();
        waiter->lease = lease;
        waiter->done = std::move(done);
        if (state.config.target.embedded())
            waiter->deadline =
                timers.after(default_deadline(OpKind::BackendReady), [this, id] { fail_waiter(id, not_ready()); });
        Waiter& stored = *waiters.emplace(id, std::move(waiter)).first->second;
        stored.cancel = token.on_cancel([this, alive_token = alive.token(), id](CancelReason) {
            strand.post([this, alive_token, id] {
                if (!alive_token.cancelled())
                    fail_waiter(id, backend_diag(msg::kCancelled).kind(ErrorKind::Cancelled).build());
            });
        });
    }

    // --- leases and reconfiguration

    void want_start(Operation<BackendUpstream>* op) {
        if (!current && state.phase == BackendPhase::Running) {
            if (op != nullptr) complete_with_health(op, false);
            return;
        }
        Command command;
        if (op != nullptr) command.start_ops.push_back(op);
        enqueue(std::move(command));
    }

    void stop(BackendStopCause cause, Operation<void>* op, UniqueFunction<void(Result<void>)> done) {
        Command command;
        command.kind = CommandKind::Stop;
        command.cause = cause;
        if (op != nullptr) command.stop_ops.push_back(op);
        if (done) command.stop_dones.push_back(std::move(done));
        enqueue(std::move(command));
    }

    void apply_config(BackendConfig config) {
        const bool restart = heading_up() || state.phase == BackendPhase::Restarting;
        state.config = std::move(config);
        state.pending_config.reset();
        publish(BackendChange::Reconfigured);
        if (!restart) return;
        stop(BackendStopCause::Reconfigure, nullptr, nullptr);
        if (state.pinned) want_start(nullptr);
    }

    void set_pinned(bool pinned) {
        if (state.pinned == pinned) return;
        state.pinned = pinned;
        publish(BackendChange::LeasesChanged);
    }

    void release(u64 id) {
        const auto found = leases.find(id);
        if (found == leases.end()) return;
        const LeaseRecord lease = found->second;
        leases.erase(found);
        state.leases = static_cast<u32>(leases.size());
        publish(BackendChange::LeasesChanged);
        for (auto& waiter : take_waiters([id](const Waiter& w) { return w.lease == id; }))
            waiter->done(std::unexpected(backend_diag(msg::kLeaseReleased).kind(ErrorKind::Cancelled).build()));
        if (lease.session && lease.embedded)
            process.end_session(*lease.session, [session = *lease.session](Result<void> ended) {
                if (!ended)
                    log(LogLevel::Warn,
                        std::format("EndSession for {} failed: {}", format_uuid(session.value), ended.error().id));
            });
        if (!leases.empty()) return;
        if (state.pending_config) return apply_config(*std::exchange(state.pending_config, std::nullopt));
        if (!state.pinned && (heading_up() || state.phase == BackendPhase::Restarting))
            stop(BackendStopCause::LastLease, nullptr, nullptr);
    }

    [[nodiscard]] u64 add_lease(std::optional<SessionId> session) {
        const u64 id = next_lease++;
        leases.emplace(id, LeaseRecord{session, state.config.target.embedded()});
        state.leases = static_cast<u32>(leases.size());
        publish(BackendChange::LeasesChanged);
        if (!heading_up()) want_start(nullptr);
        return id;
    }

    // --- IBackendProcessObserver

    void on_ready(const BackendReady& ready) override {
        if (!state.config.target.embedded() || (current && current->kind == CommandKind::Stop)) return;
        state.generation = ready.generation;
        state.version = ready.build;
        state.upstream = embedded_upstream(ready.http_port);
        if (!current) current = Command{};
        become_running(false);
    }

    void on_exit(const process::ChildExitInfo& exit) override {
        state.upstream.reset();
        if (exit.cause == process::ChildExitCause::Requested) {
            if (current && current->kind == CommandKind::Stop) return finish_stop();
            if (current && ready_timed_out) return fail_start(not_ready());
            if (state.phase != BackendPhase::Stopped) {
                state.phase = BackendPhase::Stopped;
                publish(BackendChange::Stopped);
            }
            return;
        }
        Diagnostic error = crash_error(exit);
        state.last_error = error;
        if (exit.after == process::AfterExit::Restarting) {
            publish(BackendChange::Crashed);
            state.phase = BackendPhase::Restarting;
            // A restart is a start in progress: ops and waiters join it, and a stop supersedes it.
            if (!current) current = Command{};
            arm_ready_deadline(exit.restart_delay + default_deadline(OpKind::BackendReady));
            publish(BackendChange::Restarting);
            return;
        }
        if (current && current->kind == CommandKind::Start) return fail_start(std::move(error));
        state.phase = BackendPhase::Failed;
        publish(BackendChange::Failed);
    }

    void on_login_observed(const LoginObservedEvent& event) override {
        if (login_observer) login_observer(event);
    }

    BackendProcess& process;
    RemoteBackendProbe& probe;
    IBackendSessions& sessions;
    OpRegistry& ops;
    UserRequestRegistry& requests;
    net::HostTlsMemory& tls;
    EventBus& events;
    Executor& strand;
    TimerService& timers;

    BackendState state;
    bool shutting_down = false;
    std::map<u64, LeaseRecord> leases;
    u64 next_lease = 1;
    std::map<u64, std::unique_ptr<Waiter>> waiters;
    u64 next_waiter = 1;

    std::optional<Command> current;
    std::deque<Command> queue;
    // Cancelled when a transition is superseded: late probe answers are dropped and a pending
    // ConfirmUnencryptedUpstream is withdrawn.
    CancelSource transition;
    TimerHandle ready_timer;
    bool ready_timed_out = false;
    // Start ops whose health check is in flight.
    std::vector<Operation<BackendUpstream>*> checking;

    UniqueFunction<void(const LoginObservedEvent&)> login_observer;
    std::vector<UniqueFunction<void(const BackendState&)>> ready_listeners;
    CancelSource alive;
};

BackendService::BackendService(BackendConfig config, BackendProcess& process, RemoteBackendProbe& probe,
                               IBackendSessions& sessions, OpRegistry& ops, UserRequestRegistry& requests,
                               net::HostTlsMemory& tls, EventBus& events, Executor& strand, TimerService& timers)
    : impl_(std::make_unique<Impl>(std::move(config), process, probe, sessions, ops, requests, tls, events, strand,
                                   timers)) {
    process.set_observer(impl_.get());
}

BackendService::~BackendService() {
    Impl& impl = *impl_;
    impl.alive.cancel(CancelReason::Shutdown);
    impl.transition.cancel(CancelReason::Shutdown);
    impl.process.set_observer(nullptr);
    std::vector<Impl::Command> commands(std::make_move_iterator(impl.queue.begin()), std::make_move_iterator(impl.queue.end()));
    if (impl.current) commands.push_back(std::move(*impl.current));
    for (Impl::Command& command : commands) {
        for (Operation<BackendUpstream>* op : command.start_ops) op->complete(Cancelled{CancelReason::Shutdown});
        for (Operation<void>* op : command.stop_ops) op->complete(Cancelled{CancelReason::Shutdown});
    }
    for (Operation<BackendUpstream>* op : impl.checking) op->complete(Cancelled{CancelReason::Shutdown});
}

const BackendState& BackendService::state() const noexcept { return impl_->state; }

u32 BackendService::session_leases() const noexcept { return impl_->session_leases(); }

Result<ReconfigureTiming> BackendService::reconfigure(BackendConfig config, RunningPolicy running) {
    Impl& impl = *impl_;
    if (impl.shutting_down) return std::unexpected(impl.shutting_down_error());
    if (config == impl.state.config) {
        if (impl.state.pending_config) {
            impl.state.pending_config.reset();
            impl.publish(BackendChange::Reconfigured);
        }
        return ReconfigureTiming::Now;
    }
    if (impl.leases.empty()) {
        impl.apply_config(std::move(config));
        return ReconfigureTiming::Now;
    }
    std::vector<SessionId> live = impl.leased_sessions();
    if (!live.empty() && running == RunningPolicy::Refuse) return std::unexpected(impl.in_use(live.size()));
    impl.state.pending_config = config;
    impl.publish(BackendChange::Reconfigured);
    if (!live.empty())
        impl.sessions.stop_sessions(std::move(live), [&impl, alive = impl.alive.token(), config](Result<void> stopped) {
            if (alive.cancelled() || stopped || impl.state.pending_config != config) return;
            impl.state.pending_config.reset();
            impl.state.last_error = std::move(stopped.error());
            impl.publish(BackendChange::Reconfigured);
        });
    return ReconfigureTiming::AfterLeases;
}

Result<OpHandle> BackendService::start_backend(bool pin, DisconnectPolicy policy) {
    Impl& impl = *impl_;
    if (impl.shutting_down) return std::unexpected(impl.shutting_down_error());
    // A scheme-less probe may spend a full HttpSmall budget on https before it tries http.
    const std::optional<std::chrono::milliseconds> deadline =
        impl.state.config.target.embedded() ? std::nullopt
                                            : std::optional(2 * default_deadline(OpKind::HttpSmall));
    auto [handle, op] = impl.ops.create<BackendUpstream>(OpKind::BackendReady, policy, std::nullopt,
                                                         RunnerMultiplier::Native, deadline);
    if (pin) impl.set_pinned(true);
    impl.want_start(&op);
    return handle;
}

Result<OpHandle> BackendService::start_stop(DisconnectPolicy policy) {
    Impl& impl = *impl_;
    if (const u32 live = impl.session_leases(); live != 0) return std::unexpected(impl.in_use(live));
    // Covers the grace and the kill after it.
    auto [handle, op] = impl.ops.create<void>(OpKind::GracefulStop, policy, std::nullopt, RunnerMultiplier::Native,
                                              2 * default_deadline(OpKind::GracefulStop));
    impl.set_pinned(false);
    impl.stop(BackendStopCause::UserStop, &op, nullptr);
    return handle;
}

Result<BackendLease> BackendService::acquire(SessionId session) {
    Impl& impl = *impl_;
    if (impl.shutting_down) return std::unexpected(impl.shutting_down_error());
    if (impl.state.pending_config)
        return backend_diag(msg::kReconfiguring).kind(ErrorKind::Conflict).retryable().fail();
    const BackendConfig config = impl.state.config;
    return BackendLease(*this, impl.add_lease(session), session, config);
}

Result<BackendLease> BackendService::acquire_maintenance() {
    Impl& impl = *impl_;
    if (impl.shutting_down) return std::unexpected(impl.shutting_down_error());
    if (!impl.state.config.target.embedded())
        return backend_diag(msg::kEmbeddedOnly).kind(ErrorKind::Unsupported).fail();
    if (impl.state.pending_config)
        return backend_diag(msg::kReconfiguring).kind(ErrorKind::Conflict).retryable().fail();
    const BackendConfig config = impl.state.config;
    return BackendLease(*this, impl.add_lease(std::nullopt), std::nullopt, config);
}

void BackendService::ensure_ready(const BackendLease& lease, CancelToken token,
                                  UniqueFunction<void(Result<BackendUpstream>)> done) {
    Impl& impl = *impl_;
    if (lease.service_ != this || !impl.leases.contains(lease.id_))
        return impl.answer_later(std::move(done), Result<BackendUpstream>(std::unexpected(
                                                      backend_diag(msg::kLeaseReleased).kind(ErrorKind::Cancelled).build())));
    if (token.cancelled())
        return impl.answer_later(std::move(done), Result<BackendUpstream>(std::unexpected(
                                                      backend_diag(msg::kCancelled).kind(ErrorKind::Cancelled).build())));
    if (impl.start_pending()) return impl.add_waiter(lease.id_, std::move(token), std::move(done));
    if (impl.state.phase == BackendPhase::Running) return impl.check_health(false, std::move(done), std::move(token));
    Diagnostic error = impl.state.last_error.value_or(backend_diag(msg::kStoppedBeforeReady).build());
    impl.answer_later(std::move(done), Result<BackendUpstream>(std::unexpected(std::move(error))));
}

void BackendService::configure_session(const BackendLease& lease, BackendSessionConfig config,
                                       UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (lease.service_ != this || !impl.leases.contains(lease.id_))
        return impl.answer_later(std::move(done), Result<void>(std::unexpected(
                                                      backend_diag(msg::kLeaseReleased).kind(ErrorKind::Cancelled).build())));
    if (!lease.config_.target.embedded()) return impl.answer_later(std::move(done), Result<void>{});
    if (!lease.session_)
        return impl.answer_later(std::move(done),
                                 Result<void>(std::unexpected(internal_bug("backend.configure_maintenance_lease"))));
    impl.process.configure_session(*lease.session_, std::move(config), std::move(done));
}

void BackendService::mint_launch_credential(const BackendLease& lease, LaunchCredentialRequest request,
                                            UniqueFunction<void(Result<LaunchCredential>)> done) {
    Impl& impl = *impl_;
    if (lease.service_ != this || !impl.leases.contains(lease.id_))
        return impl.answer_later(std::move(done), Result<LaunchCredential>(std::unexpected(
                                                      backend_diag(msg::kLeaseReleased).kind(ErrorKind::Cancelled).build())));
    if (!lease.config_.target.embedded())
        return impl.answer_later(std::move(done), Result<LaunchCredential>(std::unexpected(
                                                      backend_diag(msg::kEmbeddedOnly).kind(ErrorKind::Unsupported).build())));
    if (!lease.session_)
        return impl.answer_later(std::move(done),
                                 Result<LaunchCredential>(std::unexpected(internal_bug("backend.mint_maintenance_lease"))));
    impl.process.mint_launch_credential(*lease.session_, std::move(request), std::move(done));
}

void BackendService::stop_for(BackendStopCause cause, UniqueFunction<void(Result<void>)> done) {
    Impl& impl = *impl_;
    if (cause == BackendStopCause::Reset) {
        if (const u32 live = impl.session_leases(); live != 0)
            return impl.answer_later(std::move(done), Result<void>(std::unexpected(impl.in_use(live))));
    }
    if (cause == BackendStopCause::Shutdown) impl.shutting_down = true;
    impl.set_pinned(false);
    impl.stop(cause, nullptr, std::move(done));
}

void BackendService::set_login_observer(UniqueFunction<void(const LoginObservedEvent&)> observer) {
    impl_->login_observer = std::move(observer);
}

void BackendService::add_ready_listener(UniqueFunction<void(const BackendState&)> listener) {
    impl_->ready_listeners.push_back(std::move(listener));
}

void BackendService::release(u64 lease_id) { impl_->release(lease_id); }

}  // namespace rb::backend
