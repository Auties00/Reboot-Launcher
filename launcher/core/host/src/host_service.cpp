#include "reboot/host/host_service.hpp"

#include <algorithm>
#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "messages.hpp"
#include "reboot/builds/library.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/user_request.hpp"
#include "reboot/gameserver/game_server_process.hpp"
#include "reboot/gameserver/socket_role.hpp"
#include "reboot/host/host_backend_link.hpp"
#include "reboot/host/host_error.hpp"
#include "reboot/host/host_port_allocator.hpp"
#include "reboot/host/host_profile_store.hpp"
#include "reboot/host/host_profiles_changed.hpp"
#include "reboot/host/untested_host_prompt.hpp"
#include "reboot/identity/account_record.hpp"
#include "reboot/identity/identity_service.hpp"
#include "reboot/net/port_mapper_service.hpp"
#include "reboot/net/port_owner_service.hpp"
#include "reboot/net/udp_beacon_prober.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/publish/host_identity_store.hpp"
#include "reboot/publish/host_publisher.hpp"
#include "reboot/sessions/session_driver.hpp"
#include "reboot/sessions/session_registry.hpp"
#include "reboot/storage/settings.hpp"
#include "reboot/support/support_policy.hpp"

namespace reboot::host {

namespace {

namespace gs = gameserver;

// The readiness checks retry at this pace until the deadline, then slower for a late answer.
constexpr std::chrono::seconds kReadinessRetry{1};
constexpr std::chrono::seconds kLateReadinessRetry{10};
// The op's liveness bound beyond the listen deadline, for the describe and the backend lease.
constexpr std::chrono::seconds kOpDeadlineMargin{30};
constexpr std::size_t kMappingEventBudget = std::size_t{64} * 1024;
constexpr MessageId kPublishProfileBusy{"publish.profile_busy"};
// An account-only ban matches every IPv4 client claiming that id; the server binds IPv4 only.
constexpr std::string_view kAnyIpv4 = "0.0.0.0/0";

const IpAddress kLoopback = IpAddress::v4(0x7F000001);
const IpAddress kWildcard = IpAddress::v4(0);

[[nodiscard]] Diagnostic error_of(HostError error) { return to_diagnostic(error); }

[[nodiscard]] std::string key_of(const Uuid& id) { return format_uuid(id); }

[[nodiscard]] HostMatchState match_state_of(gs::MatchState state) noexcept {
    switch (state) {
        case gs::MatchState::Lobby: return HostMatchState::Lobby;
        case gs::MatchState::Warmup: return HostMatchState::Warmup;
        case gs::MatchState::InProgress: return HostMatchState::InProgress;
        case gs::MatchState::Ending: return HostMatchState::Ending;
    }
    return HostMatchState::Lobby;
}

[[nodiscard]] bool is_failure(sessions::StopReason reason) noexcept {
    return reason == sessions::StopReason::LaunchFailed || reason == sessions::StopReason::Crashed ||
           reason == sessions::StopReason::Unresponsive || reason == sessions::StopReason::Fatal;
}

[[nodiscard]] std::vector<std::string> cidr_texts(const OperatorPolicy& policy) {
    std::vector<std::string> out;
    out.reserve(policy.operator_cidrs.size());
    for (const IpCidr& cidr : policy.operator_cidrs) out.push_back(cidr.to_string());
    return out;
}

[[nodiscard]] std::vector<gs::Ban> wire_bans(const OperatorPolicy& policy, std::chrono::system_clock::time_point now) {
    std::vector<gs::Ban> out;
    for (const HostBan& ban : active_bans(policy, now)) {
        gs::Ban wire;
        wire.address = ban.address ? ban.address->to_string() : std::string(kAnyIpv4);
        wire.account_id = ban.account_id;
        wire.reason = ban.reason;
        if (ban.expires)
            wire.expires_unix_ms = static_cast<u64>(std::max<i64>(
                0, std::chrono::duration_cast<std::chrono::milliseconds>(ban.expires->time_since_epoch()).count()));
        out.push_back(std::move(wire));
    }
    return out;
}

[[nodiscard]] std::optional<SecretString> copy_secret(const std::optional<SecretString>& secret) {
    if (!secret) return std::nullopt;
    return SecretString{std::string(secret->reveal())};
}

}  // namespace

struct HostService::Impl {
    struct Session {
        // Stable across the session's life, also before the registry gave it an id.
        u64 key = 0;
        std::optional<SessionId> id;
        // Pinned at start; live edits change only the operators, listing, server name and description.
        HostProfile profile;
        HostListing listing = HostListing::Unlisted;
        PortPolicy port;
        std::optional<BuildId> build;
        gs::GameTarget target;
        storage::SettingsSnapshot settings;
        HostUpdatePolicy update_policy = HostUpdatePolicy::AfterMatch;
        std::optional<SessionId> linked_to;
        sessions::Lease lease;
        bool untested_confirmed = false;
        std::optional<gs::DescribedBinary> binary;
        std::vector<gs::SocketRole> roles;
        std::optional<SecretString> password;
        std::optional<gs::BackendAccess> backend;
        bool backend_leased = false;

        // Null once the start op has its outcome.
        Operation<SessionId>* op = nullptr;
        CancelRegistration op_cancel;
        // Cancelled when the session stops, ending every pending step.
        CancelSource work;

        std::unique_ptr<gs::GameServerProcess> server;
        u64 server_gen = 0;
        sessions::Incarnation incarnation;
        std::optional<net::OurProcess> process;
        std::optional<PortBlock> block;
        std::optional<HostListening> listening;
        // Waiting for the current server's exit before spawning again; `respawn_above` moves the
        // Auto block above the one the server could not bind.
        bool respawning = false;
        std::optional<PortBlock> respawn_above;

        TimerHandle deadline;
        bool deadline_passed = false;
        CancelSource readiness;
        TimerHandle readiness_retry;
        bool ready = false;
        std::optional<Port> unowned;

        std::optional<PortBlock> mapped;
        bool awaiting_mapping = false;
        bool published = false;
        std::optional<Port> advertised;
        std::optional<Diagnostic> degraded;

        HostPhase phase = HostPhase::Preparing;
        std::optional<Diagnostic> reason;
        // The registry saw Running; a respawn's early phases do not move it back.
        bool running = false;
        // What a reset or a cancelled match-end timer returns to.
        HostPhase resume_phase = HostPhase::Live;
        std::optional<Diagnostic> resume_reason;

        MatchEvent match;
        bool match_end_handled = false;
        TimerHandle match_end_timer;
        std::optional<gs::MatchEnded> last_result;

        bool draining = false;
        bool stopping = false;
        bool files_removed = false;
        int stop_steps = 0;
        bool awaiting_exit = false;
        std::optional<sessions::StopRequest> stop_request;
        sessions::StopDone stop_done;
        std::vector<UniqueFunction<void()>> on_ended;
    };

    class Driver final : public sessions::ISessionDriver {
    public:
        Driver(Impl& impl, u64 key, CancelToken alive) : impl_(impl), key_(key), alive_(std::move(alive)) {}
        ~Driver() override {
            if (!alive_.cancelled()) impl_.drop_session(key_);
        }
        Driver(const Driver&) = delete;
        Driver& operator=(const Driver&) = delete;

        void stop(const sessions::StopRequest& request, sessions::StopDone done) override {
            if (alive_.cancelled()) {
                done(Result<void>{});
                return;
            }
            impl_.stop_session(key_, request, std::move(done));
        }

    private:
        Impl& impl_;
        u64 key_;
        CancelToken alive_;
    };

    explicit Impl(HostServiceDeps deps_in) : deps(std::move(deps_in)) {
        mapping_events = deps.events.subscribe(EventFilter{.kinds = {EventKind::PortMappingChanged}}, kMappingEventBudget);
        mapping_events->set_notify([this] { post([this] { drain_mapping_events(); }); });
    }

    ~Impl() {
        alive.cancel(CancelReason::Shutdown);
        mapping_events->set_notify({});
        for (const auto& session : sessions)
            if (Operation<SessionId>* op = std::exchange(session->op, nullptr))
                op->complete(Failed{error_of({.code = HostErrorCode::Cancelled})});
    }

    // Callbacks from other services and posted tasks run only while the service lives.
    template <class F>
    auto guard(F&& f) {
        return [alive_token = alive.token(), f = std::forward<F>(f)](auto&&... args) mutable {
            if (!alive_token.cancelled()) f(std::forward<decltype(args)>(args)...);
        };
    }

    template <class F>
    void post(F&& task) {
        deps.strand.post(guard(std::forward<F>(task)));
    }

    [[nodiscard]] Session* find(u64 key) const {
        const auto it = std::ranges::find(sessions, key, [](const auto& session) { return session->key; });
        return it == sessions.end() ? nullptr : it->get();
    }

    [[nodiscard]] Session* find(const SessionId& id) const {
        const auto it = std::ranges::find(sessions, std::optional(id), [](const auto& session) { return session->id; });
        return it == sessions.end() ? nullptr : it->get();
    }

    [[nodiscard]] Result<Session*> host_session(const SessionId& id) const {
        Session* session = find(id);
        if (session == nullptr || session->stopping)
            return std::unexpected(error_of({.code = HostErrorCode::NotHostSession, .session = id}));
        return session;
    }

    void erase(u64 key) {
        std::erase_if(sessions, [key](const auto& session) { return session->key == key; });
    }

    [[nodiscard]] std::vector<net::OurProcess> ours() const {
        std::vector<net::OurProcess> out;
        for (const auto& session : sessions)
            if (session->process) out.push_back(*session->process);
        return out;
    }

    [[nodiscard]] bool server_running(const Session& s) const {
        if (!s.server || s.stopping || s.phase == HostPhase::Restarting) return false;
        const gs::GameServerPhase phase = s.server->phase();
        return phase == gs::GameServerPhase::AwaitingListen || phase == gs::GameServerPhase::Listening;
    }

    void set_phase(Session& s, HostPhase phase, std::optional<Diagnostic> reason = std::nullopt) {
        s.phase = phase;
        s.reason = std::move(reason);
        if (!s.id) return;
        deps.events.publish(EventKind::HostPhaseChanged, HostPhaseChanged{*s.id, phase, s.reason},
                            EventScope{.session = s.id, .op = {}, .coalesce_key = key_of(s.id->value)});
        const std::optional<sessions::SessionPhase> registry_phase = session_phase_for(phase);
        if (!registry_phase) return;
        if (*registry_phase == sessions::SessionPhase::Running) s.running = true;
        else if (s.running) return;
        (void)deps.sessions.set_phase(*s.id, *registry_phase);
    }

    // A step on the way to Live, kept out of view while a drain or a pending match-end action holds the phase.
    void step_phase(Session& s, HostPhase phase) {
        if (s.draining || s.match_end_timer.active()) return;
        set_phase(s, phase);
    }

    void publish_match(Session& s, MatchEvent event) {
        s.match = event;
        deps.events.publish(EventKind::MatchEvent, std::move(event),
                            EventScope{.session = s.id, .op = {}, .coalesce_key = {}});
    }

    void publish_profiles_changed(HostProfileChange change, std::optional<HostProfileId> profile) {
        std::string key = profile ? key_of(profile->value) : std::string("reset");
        deps.events.publish(EventKind::HostProfilesChanged, HostProfilesChanged{change, profile},
                            EventScope{.session = {}, .op = {}, .coalesce_key = std::move(key)});
    }

    void progress(Session& s, std::string_view phase) {
        if (s.op != nullptr) s.op->progress(Progress{.phase = phase});
    }

    [[nodiscard]] Result<gs::GameTarget> resolve_build(BuildId id) const {
        Result<builds::InstalledBuild> build = deps.library.get(id);
        if (!build) return std::unexpected(std::move(build.error()));
        if (!build->version_confirmed())
            return std::unexpected(error_of({.code = HostErrorCode::BuildVersionUnknown, .name = build->name}));
        return gs::GameTarget{*build->version, build->cl.value_or(Changelist{}), build->root};
    }

    Result<OpHandle> start(HostStartRequest request) {
        Result<HostProfile> profile = deps.profiles.get(request.profile);
        if (!profile) return std::unexpected(std::move(profile.error()));
        if (Result<void> checked = check_start(request, *profile); !checked) return std::unexpected(std::move(checked.error()));
        if (!profile->is_auto())
            for (const auto& session : sessions)
                if (session->profile.id == profile->id && !session->stopping)
                    return std::unexpected(error_of({.code = HostErrorCode::ProfileBusy, .name = profile->name}));
        const auto live = std::ranges::count_if(sessions, [](const auto& session) { return !session->stopping; });
        if (static_cast<u64>(live) >= deps.options.host_limit)
            return std::unexpected(error_of({.code = HostErrorCode::HostLimitReached, .limit = deps.options.host_limit}));

        auto s = std::make_unique<Session>();
        if (const std::optional<BuildId> build = request.overrides.build ? request.overrides.build : profile->build) {
            Result<gs::GameTarget> target = resolve_build(*build);
            if (!target) return std::unexpected(std::move(target.error()));
            s->build = build;
            s->target = std::move(*target);
        } else if (profile->version) {
            s->target = gs::GameTarget{profile->version->version, profile->version->cl, std::nullopt};
        } else if (const std::optional<BuildId> selected = deps.library.selected(support::SupportRole::Host)) {
            Result<gs::GameTarget> target = resolve_build(*selected);
            if (!target) return std::unexpected(std::move(target.error()));
            s->build = selected;
            s->target = std::move(*target);
        } else {
            return std::unexpected(error_of({.code = HostErrorCode::NoBuildSelected}));
        }

        s->key = ++last_key;
        s->settings = deps.settings.snapshot();
        s->update_policy = profile->update_policy.value_or(s->settings.values.host.update_policy);
        s->listing = request.overrides.listing.value_or(profile->listing);
        s->port = request.overrides.port ? PortPolicy{PinnedPorts{*request.overrides.port}} : profile->port;
        s->linked_to = request.linked_to;
        s->lease = request.lease;
        s->untested_confirmed = request.untested_confirmed;
        s->profile = std::move(*profile);

        auto [handle, op] = deps.ops.create<SessionId>(OpKind::Host, request.disconnect, std::nullopt,
                                                       RunnerMultiplier::Native,
                                                       deps.options.readiness.deadline + kOpDeadlineMargin);
        s->op = &op;
        const u64 key = s->key;
        sessions.push_back(std::move(s));
        // Registered last: an op cancelled at once still finds its session.
        sessions.back()->op_cancel = op.token().on_cancel([this, alive_token = alive.token(), key](CancelReason reason) {
            if (!alive_token.cancelled()) post([this, key, reason] { on_start_cancelled(key, reason); });
        });
        progress(*sessions.back(), "describing");
        deps.binary.describe(guard([this, key](Result<gs::DescribedBinary> described) {
            on_described(key, std::move(described));
        }));
        return handle;
    }

    void complete_op(Session& s, Outcome<SessionId> outcome) {
        Operation<SessionId>* op = std::exchange(s.op, nullptr);
        if (op == nullptr) return;
        s.op_cancel.reset();
        op->complete(std::move(outcome));
    }

    // Any failure before Listening: the op fails and the session, if open, stops with LaunchFailed.
    void fail_launch(Session& s, Diagnostic error) {
        complete_op(s, Failed{error});
        if (!s.id) {
            erase(s.key);
            return;
        }
        if (s.stopping) return;
        (void)deps.sessions.stop(
            *s.id,
            sessions::StopRequest{.reason = sessions::StopReason::LaunchFailed, .grace = deps.options.stop_grace,
                                  .error = std::move(error)},
            nullptr);
    }

    void on_start_cancelled(u64 key, CancelReason reason) {
        Session* s = find(key);
        if (s == nullptr || s->op == nullptr) return;
        if (reason == CancelReason::Deadline) {
            HostError timeout{.code = HostErrorCode::ListenTimeout};
            return fail_launch(*s, error_of(std::move(timeout)));
        }
        complete_op(*s, Cancelled{reason});
        if (!s->id) return erase(key);
        (void)deps.sessions.stop(
            *s->id, sessions::StopRequest{.reason = sessions::StopReason::User, .grace = deps.options.stop_grace},
            nullptr);
    }

    void on_described(u64 key, Result<gs::DescribedBinary> described) {
        Session* s = find(key);
        if (s == nullptr || s->op == nullptr) return;
        if (!described) return fail_launch(*s, std::move(described.error()));
        Result<support::HostInputs> inputs = support::host_inputs_from(described->sha256, described->description);
        if (!inputs) return fail_launch(*s, std::move(inputs.error()));
        s->roles = gs::socket_roles(described->description);
        s->binary = std::move(*described);

        support::SupportQuery query;
        query.version = s->target.version;
        query.cl = s->target.cl;
        query.role = support::SupportRole::Host;
        query.runner = ports::RunnerKind::Native;
        query.imported = s->build.has_value();
        query.server = std::move(*inputs);
        const support::SupportVerdict verdict = deps.support.evaluate(query);
        if (Result<void> allowed = support::check_not_blocked(query, verdict); !allowed)
            return fail_launch(*s, std::move(allowed.error()));
        if (verdict.tier == support::SupportTier::Untested && !s->untested_confirmed)
            return ask_untested(*s, std::move(query), verdict);
        open_session(*s);
    }

    void ask_untested(Session& s, support::SupportQuery query, support::SupportVerdict verdict) {
        const u64 key = s.key;
        const RequestId request = deps.requests.ask(
            UserRequestKind::ConfirmUntested, UntestedHostPrompt{s.profile.id, std::move(query), std::move(verdict)},
            s.op->id(), std::nullopt,
            [this, alive_token = alive.token(), key](const std::any& answer) -> Result<void> {
                const bool* host_anyway = std::any_cast<bool>(&answer);
                if (host_anyway == nullptr) return std::unexpected(error_of({.code = HostErrorCode::InvalidAnswer}));
                if (!alive_token.cancelled()) post([this, key, yes = *host_anyway] { on_untested_answer(key, yes); });
                return {};
            },
            s.op->token());
        if (s.op != nullptr) s.op->awaiting_user(request);
    }

    void on_untested_answer(u64 key, bool host_anyway) {
        Session* s = find(key);
        if (s == nullptr || s->op == nullptr) return;
        if (!host_anyway) return fail_launch(*s, error_of({.code = HostErrorCode::UntestedDeclined}));
        s->untested_confirmed = true;
        progress(*s, "preparing");
        open_session(*s);
    }

    void open_session(Session& s) {
        Result<std::optional<SecretString>> password = deps.join_password(s.profile.id);
        if (!password) return fail_launch(s, std::move(password.error()));
        s.password = std::move(*password);

        sessions::SessionSpec spec;
        spec.kind = sessions::SessionKind::Host;
        spec.lease = s.lease;
        spec.pinned.settings = s.settings;
        spec.pinned.build = s.build;
        spec.pinned.game_server_sha256 = s.binary->sha256;
        spec.parent = s.linked_to;
        spec.label = s.profile.name;
        spec.version = s.target.version;
        spec.runner = ports::RunnerKind::Native;
        spec.profile = s.profile.id;
        Result<SessionId> id = deps.sessions.open(std::move(spec), std::make_unique<Driver>(*this, s.key, alive.token()));
        if (!id) return fail_launch(s, std::move(id.error()));
        s.id = *id;
        set_phase(s, HostPhase::Preparing);

        if (!s.binary->description.capabilities.needs_backend) return reserve_block(s, std::nullopt);
        progress(s, "leasing_backend");
        s.backend_leased = true;
        const u64 key = s.key;
        deps.backend.acquire(*s.id, identity::account_id(deps.identity.record(identity::AccountRole::Host)),
                             s.work.token(), guard([this, key](Result<gs::BackendAccess> access) {
                                 Session* session = find(key);
                                 if (session == nullptr || session->stopping) return;
                                 if (!access) return fail_launch(*session, std::move(access.error()));
                                 session->backend = std::move(*access);
                                 reserve_block(*session, std::nullopt);
                             }));
    }

    void reserve_block(Session& s, std::optional<PortBlock> above) {
        progress(s, "reserving_ports");
        const u64 key = s.key;
        BlockRequest request{.session = *s.id,
                             .policy = s.port,
                             .size = static_cast<u16>(s.roles.size()),
                             .after = above,
                             .ours = ours()};
        Result<void> queued = deps.allocator.reserve(std::move(request), s.work.token(),
                                                     guard([this, key](Result<PortBlock> block) { on_block(key, std::move(block)); }));
        if (!queued) fail_launch(s, std::move(queued.error()));
    }

    void recheck_block(Session& s) {
        const u64 key = s.key;
        Result<void> queued = deps.allocator.recheck(*s.id, ours(), s.work.token(),
                                                     guard([this, key](Result<PortBlock> block) { on_block(key, std::move(block)); }));
        if (!queued) fail_launch(s, std::move(queued.error()));
    }

    void on_block(u64 key, Result<PortBlock> block) {
        Session* s = find(key);
        if (s == nullptr || s->stopping) return;
        if (!block) return fail_launch(*s, std::move(block.error()));
        s->block = *block;
        spawn(*s);
    }

    [[nodiscard]] gs::GameServerConfig server_config(const Session& s) const {
        gs::GameServerConfig config;
        config.game = s.target;
        config.listen.bind_address = kWildcard;
        config.listen.ports = s.block->ports();
        if (s.backend)
            config.backend = gs::BackendAccess{s.backend->origin, s.backend->account_id,
                                               SecretString{std::string(s.backend->service_token.reveal())}};
        config.match = s.profile.match;
        config.operator_cidrs = cidr_texts(s.profile.operators);
        config.bans = wire_bans(s.profile.operators, deps.clock.system_now());
        return config;
    }

    void spawn(Session& s) {
        set_phase(s, HostPhase::Spawning, s.phase == HostPhase::Restarting ? s.reason : std::nullopt);
        progress(s, "spawning");
        gs::GameServerConfig config = server_config(s);
        Result<process::BuiltEnv> env = deps.base_env();
        if (!env) return fail_launch(s, std::move(env.error()));

        const u64 key = s.key;
        const u64 gen = ++s.server_gen;
        s.listening.reset();
        s.process.reset();
        s.ready = false;
        s.unowned.reset();
        s.deadline_passed = false;
        s.readiness.cancel(CancelReason::Superseded);
        s.readiness = CancelSource{};
        s.readiness_retry.cancel();
        s.match_end_timer.cancel();
        s.server = std::make_unique<gs::GameServerProcess>(
            deps.launcher, deps.fs, deps.workers, deps.strand, deps.timers, deps.clock, deps.layout,
            gs::GameServerLaunch{*s.id, *s.binary, std::move(config), std::move(*env), std::nullopt},
            guard([this, key, gen](const gs::GameServerEvent& event) { on_server_event(key, gen, event); }),
            [this, alive_token = alive.token(), key, gen](const process::ChildRecord& child, process::RecordChange change) {
                if (!alive_token.cancelled()) on_record(key, gen, child, change);
            });
        s.deadline = deps.timers.after(deps.options.readiness.deadline, guard([this, key, gen] { on_deadline(key, gen); }));
        Result<void> started = s.server->start(guard([this, key, gen](Result<u32> pid) { on_spawned(key, gen, std::move(pid)); }));
        if (!started) fail_launch(s, std::move(started.error()));
    }

    [[nodiscard]] Session* current(u64 key, u64 gen) const {
        Session* s = find(key);
        return s != nullptr && s->server_gen == gen ? s : nullptr;
    }

    void on_record(u64 key, u64 gen, const process::ChildRecord& child, process::RecordChange change) {
        if (Session* s = find(key); s != nullptr && s->id) {
            if (change == process::RecordChange::Exited)
                deps.sessions.note_process_exited(
                    *s->id, sessions::SpawnedProcess{sessions::ProcessRole::GameServer, sessions::PidSpace::Host, child.pid});
            else if (s->server_gen == gen)
                s->process = net::OurProcess{child.pid, child.created};
        }
        if (deps.record) deps.record(child, change);
    }

    void on_spawned(u64 key, u64 gen, Result<u32> pid) {
        Session* s = current(key, gen);
        if (s == nullptr) return;
        if (s->stopping) {
            // A spawn stopped before it started reports no exit.
            if (!pid && s->awaiting_exit) server_gone(*s);
            return;
        }
        if (!pid) return fail_launch(*s, std::move(pid.error()));
        Result<sessions::Incarnation> incarnation = deps.sessions.note_spawned(
            *s->id, sessions::SpawnedProcess{sessions::ProcessRole::GameServer, sessions::PidSpace::Host, *pid});
        if (incarnation) s->incarnation = *incarnation;
        set_phase(*s, HostPhase::WaitingForListen, s->reason);
        progress(*s, "waiting_for_listen");
    }

    void on_server_event(u64 key, u64 gen, const gs::GameServerEvent& event) {
        Session* s = current(key, gen);
        if (s == nullptr) return;
        std::visit([&](const auto& payload) { on_event(*s, payload); }, event);
    }

    void on_event(Session& s, const gs::Listening& listening) {
        if (s.stopping) return;
        s.listening = HostListening{*s.id, *s.block, listening.bound};
        deps.events.publish(EventKind::HostListening, *s.listening, EventScope{.session = s.id, .op = {}, .coalesce_key = {}});
        set_phase(s, HostPhase::WaitingForReadiness, s.phase == HostPhase::Restarting ? s.reason : std::nullopt);
        complete_op(s, Completed<SessionId>{*s.id});
        check_readiness(s);
    }

    void on_event(Session& s, const gs::ListenFailed& failed) {
        if (s.stopping) return;
        HostError error{.code = HostErrorCode::ListenFailed, .port = failed.port};
        error.os_error = failed.os_error;
        if (failed.bound_instead)
            error.detail = "bound " + std::to_string(failed.bound_instead->value) + " instead";
        if (std::holds_alternative<PinnedPorts>(s.port)) return fail_launch(s, error_of(std::move(error)));
        // Auto moves above the block once this server has exited.
        if (!deps.sessions.begin_respawn(*s.id)) return;
        s.deadline.cancel();
        s.readiness.cancel(CancelReason::Superseded);
        s.listening.reset();
        s.respawning = true;
        s.respawn_above = s.block;
    }

    void on_event(Session& s, const gs::MatchStateChanged& changed) {
        // A server being replaced may still end its match on the way out.
        if (s.respawning) return;
        // The server returns to its lobby after MatchEnded; the pending action keeps the Ended view.
        if (changed.state == gs::MatchState::Lobby && s.match_end_timer.active()) return;
        if (changed.state == gs::MatchState::Warmup || changed.state == gs::MatchState::InProgress)
            s.match_end_handled = false;
        publish_match(s, MatchEvent{.session = *s.id, .state = match_state_of(changed.state)});
    }

    void publish_player(Session& s, PlayerChange change, const gs::Player& player, u32 count) {
        deps.events.publish(EventKind::PlayerEvent, PlayerEvent{*s.id, change, player, count},
                            EventScope{.session = s.id, .op = {}, .coalesce_key = {}});
    }

    void on_event(Session& s, const gs::PlayerJoined& joined) {
        // The roster changes after this event, so it does not hold the new player yet.
        const std::span<const gs::Player> roster = s.server->players();
        const bool known = std::ranges::any_of(roster, [&](const gs::Player& p) { return p.player_id == joined.player.player_id; });
        publish_player(s, PlayerChange::Joined, joined.player, static_cast<u32>(roster.size() + (known ? 0 : 1)));
    }

    void on_event(Session& s, const gs::PlayerLeft& left) {
        const std::size_t remaining = s.server->players().size() - 1;
        publish_player(s, PlayerChange::Left, left.player, static_cast<u32>(remaining));
        if (s.draining && remaining == 0) stop_for_update(s);
    }

    void on_event(Session& s, const gs::PlayerCountChanged& count) {
        if (s.published) (void)deps.publisher.set_players(*s.id, count.count);
    }

    void on_event(Session& s, const gs::MatchEnded& ended) {
        if (s.stopping || s.respawning) return;
        if (s.draining) return stop_for_update(s);
        if (s.match_end_handled) return;
        s.match_end_handled = true;
        s.last_result = ended;
        const MatchEndPolicy policy = s.profile.match_end;
        MatchEvent event{.session = *s.id, .state = HostMatchState::Ended, .result = ended, .action = policy.action};
        if (policy.action == MatchEndAction::None) return publish_match(s, std::move(event));
        event.fires_at = deps.clock.system_now() + policy.delay;
        publish_match(s, std::move(event));
        s.resume_phase = s.phase;
        s.resume_reason = s.reason;
        set_phase(s, HostPhase::Restarting);
        if (s.published) (void)deps.publisher.set_restarting(*s.id, true);
        const u64 key = s.key;
        const u64 gen = s.server_gen;
        s.match_end_timer = deps.timers.after(policy.delay, guard([this, key, gen] { on_match_end_timer(key, gen); }));
    }

    void on_event(Session& s, const gs::ServerFatal& fatal) {
        HostError error{.code = HostErrorCode::ServerFatal, .name = fatal.code};
        if (!fatal.detail.empty()) error.detail = fatal.detail;
        report_exit(s, sessions::ExitReason::Fatal, std::nullopt, error_of(std::move(error)));
    }

    void on_event(Session& s, const gs::ServerUnresponsive&) {
        report_exit(s, sessions::ExitReason::Unresponsive, std::nullopt,
                    error_of({.code = HostErrorCode::ServerUnresponsive}));
    }

    // A server being replaced is already on its way out; only its exit matters.
    void report_exit(Session& s, sessions::ExitReason reason, std::optional<i32> code, std::optional<Diagnostic> error) {
        if (s.stopping || s.respawning) return;
        if (!s.listening) {
            HostError exited{.code = HostErrorCode::ServerExited};
            exited.exit_code = code;
            return fail_launch(s, error ? std::move(*error) : error_of(std::move(exited)));
        }
        deps.sessions.report_exit(*s.id, s.incarnation,
                                  sessions::SessionExit{.reason = reason, .exit_code = code, .error = std::move(error)});
    }

    void on_event(Session& s, const gs::ServerExited& exited) {
        s.process.reset();
        if (s.stopping) {
            if (s.awaiting_exit) server_gone(s);
            return;
        }
        if (s.respawning) {
            s.respawning = false;
            const std::optional<PortBlock> above = std::exchange(s.respawn_above, std::nullopt);
            if (above) return reserve_block(s, above);
            return recheck_block(s);
        }
        const std::optional<i32> code = exited.status.code;
        sessions::ExitReason reason = sessions::ExitReason::Crashed;
        if (exited.cause == process::ChildExitCause::Unresponsive) reason = sessions::ExitReason::Unresponsive;
        else if (exited.cause == process::ChildExitCause::Exited && code == 0 && !exited.status.signal)
            reason = sessions::ExitReason::Exited;
        report_exit(s, reason, code, exited.error);
    }

    void check_readiness(Session& s) {
        const u64 key = s.key;
        const u64 gen = s.server_gen;
        const CancelToken token = s.readiness.token();
        if (!s.process) return probe(s);
        deps.workers.submit<std::optional<Port>>(
            [&owners = deps.port_owners, ports = s.block->ports(), process = *s.process](CancelToken) -> Result<std::optional<Port>> {
                for (const Port port : ports) {
                    Result<bool> held = owners.held_by(net::PortProtocol::Udp, Endpoint{kWildcard, port}, process);
                    if (!held) return std::unexpected(std::move(held.error()));
                    if (!*held) return port;
                }
                return std::nullopt;
            },
            token, deps.strand, guard([this, key, gen, token](Result<std::optional<Port>> unowned) {
                Session* session = current(key, gen);
                if (session == nullptr || token.cancelled()) return;
                // An unreadable socket table leaves the probe as the only check.
                if (!unowned) {
                    REBOOT_LOG_AT(LogLevel::Warn, Host, session->id, "cannot read the port owners: {}", unowned.error().id);
                } else if (*unowned) {
                    session->unowned = *unowned;
                    return retry_readiness(*session);
                } else {
                    session->unowned.reset();
                }
                probe(*session);
            }));
    }

    void probe(Session& s) {
        const u64 key = s.key;
        const u64 gen = s.server_gen;
        const CancelToken token = s.readiness.token();
        const Port port = game_port(*s.block, s.roles);
        Result<void> sent = deps.prober.probe(
            Endpoint{kLoopback, port}, deps.options.readiness.loopback_probe, token,
            guard([this, key, gen, token](net::ProbeResult result) {
                Session* session = current(key, gen);
                if (session == nullptr || token.cancelled()) return;
                if (result.outcome == net::ProbeOutcome::Alive) return on_ready(*session);
                retry_readiness(*session);
            }));
        if (!sent) {
            REBOOT_LOG_AT(LogLevel::Warn, Host, s.id, "cannot probe the game port: {}", sent.error().id);
            retry_readiness(s);
        }
    }

    void retry_readiness(Session& s) {
        const u64 key = s.key;
        const u64 gen = s.server_gen;
        const CancelToken token = s.readiness.token();
        const std::chrono::seconds delay = s.deadline_passed ? kLateReadinessRetry : kReadinessRetry;
        s.readiness_retry = deps.timers.after(delay, guard([this, key, gen, token] {
                                                  Session* session = current(key, gen);
                                                  if (session != nullptr && !token.cancelled()) check_readiness(*session);
                                              }));
    }

    void on_deadline(u64 key, u64 gen) {
        Session* s = current(key, gen);
        if (s == nullptr || s->stopping) return;
        s->deadline_passed = true;
        if (!s->listening) return fail_launch(*s, error_of({.code = HostErrorCode::ListenTimeout}));
        if (s->ready) return;
        const Port port = s->unowned.value_or(game_port(*s->block, s->roles));
        Diagnostic reason = error_of({.code = s->unowned ? HostErrorCode::PortNotOwned : HostErrorCode::ReadinessTimeout,
                                      .port = port});
        if (deps.options.readiness.on_timeout == ReadinessTimeoutAction::StopSession) {
            (void)deps.sessions.stop(*s->id,
                                     sessions::StopRequest{.reason = sessions::StopReason::LaunchFailed,
                                                           .grace = deps.options.stop_grace,
                                                           .error = std::move(reason)},
                                     nullptr);
            return;
        }
        degrade(*s, reason);
        settle(*s, HostPhase::LiveUnpublished, std::move(reason));
    }

    void degrade(Session& s, Diagnostic condition) {
        s.degraded = condition;
        (void)deps.sessions.raise_degraded(*s.id, std::move(condition));
    }

    void clear_degraded(Session& s) {
        if (const std::optional<Diagnostic> condition = std::exchange(s.degraded, std::nullopt))
            (void)deps.sessions.clear_degraded(*s.id, condition->id);
    }

    // Live or LiveUnpublished, unless a drain or a pending match-end action holds the phase.
    void settle(Session& s, HostPhase phase, std::optional<Diagnostic> reason) {
        s.resume_phase = phase;
        s.resume_reason = reason;
        if (s.draining || s.match_end_timer.active()) return;
        set_phase(s, phase, std::move(reason));
    }

    void on_ready(Session& s) {
        s.ready = true;
        s.deadline.cancel();
        s.readiness_retry.cancel();
        clear_degraded(s);
        if (!s.profile.port_mapping || s.mapped == s.block) return publish_or_resume(s, granted_port(s));
        step_phase(s, HostPhase::MappingPorts);
        s.awaiting_mapping = true;
        if (!s.mapped) return map_block(s);
        // A respawn moved the block: the old mapping goes first.
        s.mapped.reset();
        const u64 key = s.key;
        deps.mapper.unmap(*s.id, guard([this, key] {
            if (Session* session = find(key); session != nullptr && !session->stopping && session->awaiting_mapping)
                map_block(*session);
        }));
    }

    void map_block(Session& s) {
        const Port game = game_port(*s.block, s.roles);
        std::vector<Port> ports{game};
        for (const Port port : s.block->ports())
            if (port != game) ports.push_back(port);
        if (Result<void> mapping = deps.mapper.map(*s.id, ports); !mapping) {
            REBOOT_LOG_AT(LogLevel::Warn, Host, s.id, "cannot map the port block: {}", mapping.error().id);
            s.awaiting_mapping = false;
            publish_or_resume(s, std::nullopt);
        }
    }

    [[nodiscard]] std::optional<Port> granted_port(const Session& s) const {
        if (!s.mapped) return std::nullopt;
        const Port game = game_port(*s.block, s.roles);
        for (const net::PortMapping& mapping : deps.mapper.mappings(*s.id))
            if (mapping.internal == game) return mapping.external;
        return std::nullopt;
    }

    void drain_mapping_events() {
        std::vector<EventEnvelope> batch;
        while (mapping_events->drain(batch, 64) > 0) {
            for (const EventEnvelope& event : batch)
                if (const auto* changed = std::any_cast<net::PortMappingChanged>(&event.payload)) on_mapping(*changed);
            batch.clear();
        }
        if (mapping_events->take_resync())
            for (const auto& session : sessions)
                if (session->awaiting_mapping && !session->stopping) {
                    session->awaiting_mapping = false;
                    session->mapped = session->block;
                    publish_or_resume(*session, granted_port(*session));
                }
    }

    void on_mapping(const net::PortMappingChanged& changed) {
        Session* s = find(changed.session);
        if (s == nullptr || s->stopping || !s->block) return;
        // The removal of a block the session moved away from does not answer the new block's mapping.
        if (changed.game_port != game_port(*s->block, s->roles)) return;
        if (s->awaiting_mapping) {
            s->awaiting_mapping = false;
            s->mapped = s->block;
            return publish_or_resume(*s, changed.granted_game_port());
        }
        if (!s->published) return;
        const Port port = changed.granted_game_port().value_or(game_port(*s->block, s->roles));
        if (port == s->advertised) return;
        s->advertised = port;
        (void)deps.publisher.set_game_port(*s->id, port);
    }

    [[nodiscard]] std::string server_name(const HostProfile& profile) const {
        if (!profile.server_name.empty()) return profile.server_name;
        if (!deps.options.default_server_name.empty()) return deps.options.default_server_name;
        return publish::author_for(deps.identity.record(identity::AccountRole::Host));
    }

    void publish_or_resume(Session& s, std::optional<Port> granted) {
        const Port port = granted.value_or(game_port(*s.block, s.roles));
        if (s.published) {
            if (port != s.advertised) {
                s.advertised = port;
                (void)deps.publisher.set_game_port(*s.id, port);
            }
            if (!s.draining) (void)deps.publisher.set_restarting(*s.id, false);
            return settle(s, HostPhase::Live, std::nullopt);
        }
        step_phase(s, HostPhase::Publishing);
        publish::PublishRequest request;
        request.session = *s.id;
        request.profile = s.profile.id;
        request.metadata = publish::HostMetadata{.name = server_name(s.profile),
                                                 .description = s.profile.description,
                                                 .author = publish::author_for(deps.identity.record(identity::AccountRole::Host)),
                                                 .version = s.target.version,
                                                 .max_players = s.profile.match.max_players};
        request.password = copy_secret(s.password);
        request.listing = s.listing;
        request.game_port = port;
        request.players = static_cast<u32>(s.server ? s.server->players().size() : 0);
        Result<void> published = deps.publisher.publish(std::move(request));
        if (!published) {
            Diagnostic reason = std::move(published.error());
            if (s.profile.is_auto() && reason.is(kPublishProfileBusy))
                reason = make_diag(ErrorDomain::Host, msg::kSecondAutoServerUnpublished).cause(std::move(reason)).build();
            return settle(s, HostPhase::LiveUnpublished, std::move(reason));
        }
        s.published = true;
        s.advertised = port;
        if (s.draining || s.match_end_timer.active()) (void)deps.publisher.set_restarting(*s.id, true);
        settle(s, HostPhase::Live, std::nullopt);
    }

    void on_match_end_timer(u64 key, u64 gen) {
        Session* s = current(key, gen);
        if (s == nullptr || s->stopping) return;
        switch (s->profile.match_end.action) {
            case MatchEndAction::Shutdown:
                (void)deps.sessions.stop(*s->id,
                                         sessions::StopRequest{.reason = sessions::StopReason::MatchEnded,
                                                               .grace = deps.options.stop_grace},
                                         nullptr);
                return;
            case MatchEndAction::Restart: break;
            case MatchEndAction::None: return;
        }
        if (restart_method(s->binary->description.capabilities) == RestartMethod::Respawn || !s->server)
            return respawn(*s, std::nullopt);
        Result<void> sent = s->server->request(gs::ResetMatch{}, guard([this, key, gen](gs::CommandResult result) {
            Session* session = current(key, gen);
            if (session == nullptr || session->stopping || session->phase != HostPhase::Restarting) return;
            if (result.status == gs::CommandStatus::Ok) return after_reset(*session);
            respawn(*session, result.error);
        }));
        if (!sent) respawn(*s, std::move(sent.error()));
    }

    void after_reset(Session& s) {
        s.match_end_handled = false;
        publish_match(s, MatchEvent{.session = *s.id, .state = HostMatchState::Lobby});
        if (s.published) (void)deps.publisher.set_restarting(*s.id, false);
        set_phase(s, s.resume_phase, s.resume_reason);
    }

    // A new process on the same block once the current one has exited.
    void respawn(Session& s, std::optional<Diagnostic> reason) {
        if (!deps.sessions.begin_respawn(*s.id)) return;
        set_phase(s, HostPhase::Restarting, std::move(reason));
        s.match_end_handled = false;
        s.listening.reset();
        s.deadline.cancel();
        s.readiness.cancel(CancelReason::Superseded);
        s.respawning = true;
        s.respawn_above.reset();
        publish_match(s, MatchEvent{.session = *s.id, .state = HostMatchState::Lobby});
        if (s.server && s.server->stop(deps.options.stop_grace)) return;
        s.respawning = false;
        recheck_block(s);
    }

    Result<bool> cancel_match_end(const SessionId& id) {
        Result<Session*> found = host_session(id);
        if (!found) return std::unexpected(std::move(found.error()));
        Session& s = **found;
        if (!s.match_end_timer.active()) return false;
        s.match_end_timer.cancel();
        publish_match(s, MatchEvent{.session = id, .state = HostMatchState::Ended, .result = s.last_result});
        if (s.published) (void)deps.publisher.set_restarting(id, false);
        set_phase(s, s.resume_phase, s.resume_reason);
        return true;
    }

    void stop_session(u64 key, const sessions::StopRequest& request, sessions::StopDone done) {
        Session* s = find(key);
        if (s == nullptr) {
            done(Result<void>{});
            return;
        }
        if (s->stopping) {
            // The registry stops a session once; this is only defensive.
            done(Result<void>{});
            return;
        }
        s->stopping = true;
        s->stop_request = request;
        s->stop_done = std::move(done);
        if (s->op != nullptr) {
            if (request.error) complete_op(*s, Failed{*request.error});
            else complete_op(*s, Cancelled{CancelReason::User});
        }
        s->work.cancel(CancelReason::Shutdown);
        s->readiness.cancel(CancelReason::Shutdown);
        s->deadline.cancel();
        s->readiness_retry.cancel();
        s->match_end_timer.cancel();
        set_phase(*s, HostPhase::Stopping, request.error);

        // One step for this setup, so a step finishing at once cannot complete the stop early.
        s->stop_steps = 1;
        if (s->published) {
            s->published = false;
            ++s->stop_steps;
            deps.publisher.withdraw(*s->id, guard([this, key] {
                if (Session* session = find(key)) step_done(*session);
            }));
        }
        s = find(key);
        if (s == nullptr) return;
        if (s->mapped || s->awaiting_mapping) {
            s->mapped.reset();
            s->awaiting_mapping = false;
            deps.mapper.unmap(*s->id, [] {});
        }
        if (s->server && s->server->stop(request.grace)) {
            s->awaiting_exit = true;
            ++s->stop_steps;
        }
        step_done(*s);
    }

    void server_gone(Session& s) {
        s.awaiting_exit = false;
        step_done(s);
    }

    void step_done(Session& s) {
        if (--s.stop_steps > 0) return;
        release(s);
        const sessions::StopRequest& request = *s.stop_request;
        if (is_failure(request.reason)) set_phase(s, HostPhase::Failed, request.error);
        else set_phase(s, HostPhase::Stopped);
        if (sessions::StopDone done = std::move(s.stop_done)) done(Result<void>{});
    }

    // What a session holds outside its server; safe to run twice.
    void release(Session& s) {
        if (!s.id) return;
        deps.allocator.release(*s.id);
        if (std::exchange(s.backend_leased, false)) deps.backend.release(*s.id);
        if (s.mapped || s.awaiting_mapping) {
            s.mapped.reset();
            s.awaiting_mapping = false;
            deps.mapper.unmap(*s.id, [] {});
        }
        if (std::exchange(s.published, false)) deps.publisher.withdraw(*s.id, [] {});
        if (s.server_gen == 0 || std::exchange(s.files_removed, true)) return;
        deps.workers.submit<void>(
            [&fs = deps.fs, dir = deps.layout.game_server_session_dir(*s.id)](CancelToken) { return fs.remove_tree(dir); },
            CancelToken{}, deps.strand, [](Result<void>) {});
    }

    // The registry destroyed the driver: the session ended, or its stop overran.
    void drop_session(u64 key) {
        Session* s = find(key);
        if (s == nullptr) return;
        // The kill comes first, so the session folder is not removed under a live server.
        s->server.reset();
        release(*s);
        complete_op(*s, Failed{error_of({.code = HostErrorCode::Cancelled})});
        for (UniqueFunction<void()>& ended : s->on_ended) deps.strand.post(std::move(ended));
        s->on_ended.clear();
        std::unique_ptr<Session> removed;
        const auto it = std::ranges::find(sessions, key, [](const auto& session) { return session->key; });
        removed = std::move(*it);
        sessions.erase(it);
    }

    void stop_for_update(Session& s) {
        (void)deps.sessions.stop(
            *s.id, sessions::StopRequest{.reason = sessions::StopReason::Update, .grace = deps.options.stop_grace},
            nullptr);
    }

    void drain(sessions::ShutdownCause cause, UniqueFunction<void()> done) {
        struct Barrier {
            std::size_t left = 0;
            UniqueFunction<void()> done;
        };
        auto barrier = std::make_shared<Barrier>();
        barrier->done = std::move(done);
        const auto join = [barrier] {
            return [barrier] {
                if (--barrier->left > 0 || !barrier->done) return;
                UniqueFunction<void()> finished = std::move(barrier->done);
                barrier->done = {};
                finished();
            };
        };
        std::vector<u64> keys;
        for (const auto& session : sessions) keys.push_back(session->key);
        for (const u64 key : keys) {
            Session* s = find(key);
            if (s == nullptr || s->stopping) continue;
            const bool after_match = cause == sessions::ShutdownCause::DrainUpdate &&
                                     s->update_policy == HostUpdatePolicy::AfterMatch;
            if (cause == sessions::ShutdownCause::DrainUpdate && !after_match) continue;
            if (!s->id) {
                // Not open yet: nothing runs, so the start just ends.
                fail_launch(*s, error_of({.code = HostErrorCode::Cancelled}));
                continue;
            }
            ++barrier->left;
            s->on_ended.push_back(join());
            if (!after_match) {
                (void)deps.sessions.stop(*s->id,
                                         sessions::StopRequest{.reason = sessions::stop_reason_for(cause),
                                                               .grace = deps.options.stop_grace},
                                         nullptr);
                continue;
            }
            begin_drain(*s);
        }
        if (barrier->left == 0 && barrier->done) {
            deps.strand.post(std::move(barrier->done));
            barrier->done = {};
        }
    }

    void begin_drain(Session& s) {
        if (s.draining) return;
        s.draining = true;
        // Restarting follows a match that already ended; before Listening no match can run.
        const bool between_matches = s.phase == HostPhase::Restarting || !s.listening;
        s.match_end_timer.cancel();
        if (between_matches || !s.server || s.server->players().empty()) return stop_for_update(s);
        if (server_running(s) && is_declared(gs::OperatorCommand{gs::Drain{}}, s.binary->description.capabilities))
            (void)s.server->request(gs::Drain{}, [](gs::CommandResult) {});
        if (s.published) (void)deps.publisher.set_restarting(*s.id, true);
        set_phase(s, HostPhase::Draining);
    }

    void push_operators(Session& s) {
        if (!server_running(s)) return;
        (void)s.server->request(gs::SetOperators{cidr_texts(s.profile.operators)}, [](gs::CommandResult) {});
        (void)s.server->request(gs::SetBans{wire_bans(s.profile.operators, deps.clock.system_now())},
                                [](gs::CommandResult) {});
    }

    // The live part of a profile edit: operators to the server, listing and texts to the edge.
    void apply_to_live(const HostProfile& profile, std::optional<u64> skip_operators) {
        for (const auto& session : sessions) {
            Session& s = *session;
            if (s.profile.id != profile.id || !s.id || s.stopping) continue;
            if (s.profile.operators != profile.operators) {
                s.profile.operators = profile.operators;
                if (s.key != skip_operators) push_operators(s);
            }
            publish::MetadataPatch patch;
            bool changed = false;
            if (s.profile.server_name != profile.server_name) {
                s.profile.server_name = profile.server_name;
                patch.name = server_name(s.profile);
                changed = true;
            }
            if (s.profile.description != profile.description) {
                s.profile.description = profile.description;
                patch.description = profile.description;
                changed = true;
            }
            // Only a listing edit replaces the session's, which may come from a start override.
            if (s.profile.listing != profile.listing) {
                s.profile.listing = profile.listing;
                if (s.listing != profile.listing) {
                    s.listing = profile.listing;
                    patch.listing = profile.listing;
                    changed = true;
                }
            }
            if (changed && s.published)
                if (Result<void> sent = deps.publisher.update(*s.id, std::move(patch)); !sent)
                    REBOOT_LOG_AT(LogLevel::Warn, Host, s.id, "cannot update the published entry: {}", sent.error().id);
        }
    }

    Result<HostProfile> update_profile(HostProfile profile) {
        Result<HostProfile> updated = deps.profiles.update(std::move(profile));
        if (!updated) return updated;
        publish_profiles_changed(HostProfileChange::Updated, updated->id);
        apply_to_live(*updated, std::nullopt);
        return updated;
    }

    Result<void> delete_profile(const HostProfileId& id) {
        for (const auto& session : sessions)
            if (session->profile.id == id && !session->stopping)
                return std::unexpected(error_of({.code = HostErrorCode::ProfileBusy, .name = session->profile.name}));
        if (Result<void> removed = deps.profiles.remove(id); !removed) return removed;
        if (Result<void> identity = deps.identities.remove(id, [](Result<void>) {}); !identity)
            REBOOT_LOG_WARN(Host, "the identity of a deleted host profile stays: {}", identity.error().id);
        publish_profiles_changed(HostProfileChange::Removed, id);
        return {};
    }

    Result<void> reset_profiles() {
        if (Result<void> reset = deps.profiles.reset(); !reset) return reset;
        publish_profiles_changed(HostProfileChange::Reset, std::nullopt);
        std::vector<HostProfileId> live;
        for (const auto& session : sessions)
            if (session->id && !session->stopping && std::ranges::find(live, session->profile.id) == live.end())
                live.push_back(session->profile.id);
        for (const HostProfileId& id : live)
            if (Result<HostProfile> profile = deps.profiles.get(id)) apply_to_live(*profile, std::nullopt);
        return {};
    }

    Result<void> refresh_join_password(const HostProfileId& profile) {
        Result<std::optional<SecretString>> password = deps.join_password(profile);
        if (!password) return std::unexpected(std::move(password.error()));
        for (const auto& session : sessions) {
            Session& s = *session;
            if (s.profile.id != profile || s.stopping) continue;
            s.password = copy_secret(*password);
            if (!s.published) continue;
            publish::MetadataPatch patch;
            patch.password = *password ? SecretString{std::string((*password)->reveal())} : SecretString{};
            if (Result<void> sent = deps.publisher.update(*s.id, std::move(patch)); !sent)
                REBOOT_LOG_AT(LogLevel::Warn, Host, s.id, "cannot publish the new join password: {}", sent.error().id);
        }
        return {};
    }

    // Stores the profile's new operator policy, then pushes it to the profile's other live sessions.
    Result<HostProfile> store_operators(Session& s, UniqueFunction<void(OperatorPolicy&)> edit) {
        Result<HostProfile> current = deps.profiles.get(s.profile.id);
        if (!current) return current;
        edit(current->operators);
        Result<HostProfile> updated = deps.profiles.update(std::move(*current));
        if (!updated) return updated;
        publish_profiles_changed(HostProfileChange::Updated, updated->id);
        apply_to_live(*updated, s.key);
        return updated;
    }

    Result<void> command(const SessionId& id, HostCommand command, UniqueFunction<void(gs::CommandResult)> done) {
        Result<Session*> found = host_session(id);
        if (!found) return std::unexpected(std::move(found.error()));
        Session& s = **found;
        if (!server_running(s)) return std::unexpected(error_of({.code = HostErrorCode::ServerNotRunning}));
        return std::visit(
            [&]<class C>(C& typed) -> Result<void> {
                if constexpr (std::is_same_v<C, ReplaceBans>) {
                    Result<HostProfile> stored = store_operators(
                        s, [bans = std::move(typed.bans)](OperatorPolicy& policy) mutable { policy.bans = std::move(bans); });
                    if (!stored) return std::unexpected(std::move(stored.error()));
                    return s.server->request(gs::SetBans{wire_bans(s.profile.operators, deps.clock.system_now())},
                                             std::move(done));
                } else if constexpr (std::is_same_v<C, ReplaceOperators>) {
                    Result<HostProfile> stored = store_operators(
                        s, [cidrs = std::move(typed.operator_cidrs)](OperatorPolicy& policy) mutable {
                            policy.operator_cidrs = std::move(cidrs);
                        });
                    if (!stored) return std::unexpected(std::move(stored.error()));
                    return s.server->request(gs::SetOperators{cidr_texts(s.profile.operators)}, std::move(done));
                } else {
                    return s.server->request(gs::OperatorCommand{std::move(typed)}, std::move(done));
                }
            },
            command);
    }

    Result<HostListening> listening(const SessionId& id) const {
        Result<Session*> found = host_session(id);
        if (!found) return std::unexpected(std::move(found.error()));
        if (!(*found)->listening) return std::unexpected(error_of({.code = HostErrorCode::NotListening, .session = id}));
        return *(*found)->listening;
    }

    Result<HostSnapshot> status(const SessionId& id) const {
        Result<Session*> found = host_session(id);
        if (!found) return std::unexpected(std::move(found.error()));
        const Session& s = **found;
        HostSnapshot snapshot;
        snapshot.phase = HostPhaseChanged{id, s.phase, s.reason};
        snapshot.listening = s.listening;
        snapshot.publish = deps.publisher.state(id);
        snapshot.reachability = deps.publisher.reachability(id);
        snapshot.mappings = deps.mapper.mappings(id);
        snapshot.match = s.match;
        snapshot.match.session = id;
        if (s.server) snapshot.players.assign(s.server->players().begin(), s.server->players().end());
        return snapshot;
    }

    HostServiceDeps deps;
    std::vector<std::unique_ptr<Session>> sessions;
    u64 last_key = 0;
    std::shared_ptr<Subscription> mapping_events;
    // Declared last: cancelled first in the destructor, so nothing posted earlier touches the rest.
    CancelSource alive;
};

HostService::HostService(HostServiceDeps deps) : impl_(std::make_unique<Impl>(std::move(deps))) {}

HostService::~HostService() = default;

std::vector<HostProfile> HostService::profiles() const { return impl_->deps.profiles.list(); }

Result<HostProfile> HostService::create_profile(HostProfile draft) {
    Result<HostProfile> created = impl_->deps.profiles.create(std::move(draft));
    if (created) impl_->publish_profiles_changed(HostProfileChange::Added, created->id);
    return created;
}

Result<HostProfile> HostService::update_profile(HostProfile profile) { return impl_->update_profile(std::move(profile)); }

Result<void> HostService::delete_profile(HostProfileId id) { return impl_->delete_profile(id); }

Result<void> HostService::reset_profiles() { return impl_->reset_profiles(); }

Result<void> HostService::refresh_join_password(HostProfileId profile) { return impl_->refresh_join_password(profile); }

Result<OpHandle> HostService::start(HostStartRequest request) { return impl_->start(std::move(request)); }

Result<void> HostService::command(SessionId session, HostCommand command,
                                  UniqueFunction<void(gameserver::CommandResult)> done) {
    return impl_->command(session, std::move(command), std::move(done));
}

Result<bool> HostService::cancel_match_end(SessionId session) { return impl_->cancel_match_end(session); }

Result<HostListening> HostService::listening(SessionId session) const { return impl_->listening(session); }

Result<publish::ShareLink> HostService::share_link(SessionId session) const {
    if (Result<Impl::Session*> found = impl_->host_session(session); !found) return std::unexpected(std::move(found.error()));
    return impl_->deps.publisher.share_link(session);
}

Result<HostPhase> HostService::phase(SessionId session) const {
    Result<Impl::Session*> found = impl_->host_session(session);
    if (!found) return std::unexpected(std::move(found.error()));
    return (*found)->phase;
}

Result<HostSnapshot> HostService::status(SessionId session) const { return impl_->status(session); }

void HostService::drain(sessions::ShutdownCause cause, UniqueFunction<void()> done) {
    impl_->drain(cause, std::move(done));
}

}  // namespace reboot::host
