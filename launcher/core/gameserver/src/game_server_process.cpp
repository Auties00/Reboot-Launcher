#include "reboot/gameserver/game_server_process.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "address_text.hpp"
#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/gameserver/game_server_error.hpp"
#include "reboot/gameserver/socket_role.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/process/child_handshake.hpp"
#include "reboot/process/child_observer.hpp"
#include "reboot/process/child_supervisor.hpp"
#include "reboot/process/process_spec.hpp"

namespace rb::gameserver {

namespace {

namespace gs = contracts::game_server;

// The supervisor's answers for a request the child never got to answer.
constexpr MessageId kChildGone{"process.child_gone"};
constexpr MessageId kChildNotRunning{"process.child_not_running"};
constexpr MessageId kChildRequestUnsupported{"process.child_request_unsupported"};

[[nodiscard]] Diagnostic error_of(GameServerError error) { return to_diagnostic(error); }

[[nodiscard]] u32 clamp_u32(i64 value) noexcept {
    if (value <= 0) return 0;
    return value > std::numeric_limits<u32>::max() ? std::numeric_limits<u32>::max() : static_cast<u32>(value);
}

[[nodiscard]] Result<void> check_command(const OperatorCommand& command) {
    const auto invalid_address = [](const std::string& address) -> Result<void> {
        return std::unexpected(error_of({.code = GameServerErrorCode::InvalidAddress, .address = address}));
    };
    if (const auto* start = std::get_if<StartMatch>(&command); start && start->countdown.count() < 0)
        return std::unexpected(error_of({.code = GameServerErrorCode::InvalidMatchSetting, .field = "countdown"}));
    if (const auto* bans = std::get_if<SetBans>(&command))
        for (const Ban& ban : bans->bans)
            if (!is_ip_or_cidr(ban.address)) return invalid_address(ban.address);
    if (const auto* operators = std::get_if<SetOperators>(&command))
        for (const std::string& cidr : operators->ip_cidrs)
            if (!is_ip_or_cidr(cidr)) return invalid_address(cidr);
    return {};
}

// The codec accepts any value of an enum's underlying type; only declared values are passed on.
template <class T>
[[nodiscard]] bool known(const T&) noexcept {
    return true;
}
[[nodiscard]] bool known(const gs::StateChanged& changed) noexcept { return changed.state <= MatchState::Ending; }
[[nodiscard]] bool known(const gs::MatchEnded& ended) noexcept { return ended.reason <= MatchEndReason::Operator; }

}  // namespace

struct GameServerProcess::Impl final {
    // Outlives the Impl while the supervisor is inside one of its calls, so the sink or a command
    // callback may destroy the process from any event.
    struct Supervised final : process::ChildObserver {
        explicit Supervised(Impl& impl) : owner(&impl) {}

        template <class Call>
        void forward(Call&& call) {
            ++depth;
            if (owner != nullptr) call(*owner);
            --depth;
        }

        void on_running(u32 generation) override {
            forward([&](Impl& impl) { impl.on_running(generation); });
        }
        void on_event(const RawFrame& frame) override {
            forward([&](Impl& impl) { impl.on_event(frame); });
        }
        void on_unresponsive(u32 missed) override {
            forward([&](Impl& impl) { impl.on_unresponsive(missed); });
        }
        void on_exit(const process::ChildExitInfo& exit) override {
            forward([&](Impl& impl) { impl.on_exit(exit); });
        }

        Impl* owner;
        int depth = 0;
        std::unique_ptr<process::ChildSupervisor> supervisor;
    };

    struct PendingCommand {
        std::string name;
        UniqueFunction<void(CommandResult)> done;
        TimerHandle timeout;
    };

    Impl(ports::IProcessLauncher& launcher, ports::IFileSystem& file_system, WorkerPool& worker_pool,
         Executor& strand_executor, TimerService& timer_service, const IClock& clock, const AppLayout& layout,
         GameServerLaunch launch, GameServerEventSink on_event, process::ChildRecordCallback record)
        : fs(file_system),
          workers(worker_pool),
          strand(strand_executor),
          timers(timer_service),
          session(launch.session),
          binary(std::move(launch.binary)),
          config(std::move(launch.config)),
          session_dir(layout.game_server_session_dir(launch.session)),
          sink(std::move(on_event)) {
        process::ProcessSpec spec;
        spec.role = process::ChildRole::GameServer;
        spec.session = session;
        spec.exe = binary.exe;
        spec.args = {"--control=stdio"};
        spec.env = std::move(launch.env);
        spec.cwd = session_dir;
        spec.stdio = ports::StdioMode::ControlChannel;
        spec.scope_name = std::move(launch.scope_name);

        process::ChildSupervisorOptions options;
        options.log_category = LogCategory::Host;

        // The block goes into runtime.json, so a busy port traces back to an orphaned server.
        process::ChildRecordCallback with_ports = [record = std::move(record), ports = config.listen.ports](
                                                      const process::ChildRecord& child,
                                                      process::RecordChange change) mutable {
            if (!record) return;
            process::ChildRecord recorded = child;
            recorded.ports = ports;
            record(recorded, change);
        };
        supervised = std::make_unique<Supervised>(*this);
        supervised->supervisor = std::make_unique<process::ChildSupervisor>(
            launcher, strand, timers, clock, std::move(spec), handshake(), std::move(options), *supervised,
            std::move(with_ports));
        supervisor = supervised->supervisor.get();
    }

    ~Impl() {
        alive.cancel(CancelReason::Shutdown);
        supervised->owner = nullptr;
        // Inside a supervisor call, its destruction (and the kill) waits until that call returns.
        if (supervised->depth > 0) strand.post([held = std::move(supervised)] {});
        else supervised.reset();
    }

    [[nodiscard]] process::ChildHandshake handshake() {
        return process::make_child_handshake<gs::ServerHello>(
            gs::kGameServerProtocol, [](const gs::ServerHello& hello) { return hello.description.protocol; },
            [held = supervised.get()](const gs::ServerHello& hello) -> Result<gs::ServerWelcome> {
                if (held->owner == nullptr) return std::unexpected(error_of({.code = GameServerErrorCode::NotRunning}));
                const Impl& impl = *held->owner;
                if (!same_description(hello.description, impl.binary.description))
                    return std::unexpected(
                        error_of({.code = GameServerErrorCode::DescriptionMismatch, .path = impl.binary.exe}));
                return gs::ServerWelcome{to_wire(impl.config, impl.session, impl.session_dir)};
            });
    }

    // False when the sink destroyed the process.
    bool emit(const GameServerEvent& event) {
        if (!sink) return true;
        const CancelToken token = alive.token();
        sink(event);
        return !token.cancelled();
    }

    Result<void> start(UniqueFunction<void(Result<u32>)> on_spawned) {
        if (phase != GameServerPhase::Idle)
            return std::unexpected(error_of({.code = GameServerErrorCode::AlreadyStarted}));
        if (Result<void> valid = validate(config, binary.description); !valid) return valid;
        phase = GameServerPhase::Preparing;
        spawned = std::move(on_spawned);
        const CancelToken token = alive.token();
        workers.submit<void>(
            [&fs = fs, dir = session_dir](CancelToken) { return fs.create_dirs_owner_only(dir); }, token, strand,
            [this, token](Result<void> created) {
                if (!token.cancelled() && phase == GameServerPhase::Preparing) on_prepared(std::move(created));
            });
        return {};
    }

    void on_prepared(Result<void> created) {
        UniqueFunction<void(Result<u32>)> done = std::move(spawned);
        if (!created) {
            phase = GameServerPhase::Exited;
            done(std::unexpected(error_of({.code = GameServerErrorCode::SessionDirFailed,
                                           .path = session_dir,
                                           .cause = std::move(created.error())})));
            return;
        }
        if (Result<void> started = supervisor->start(); !started) {
            phase = GameServerPhase::Exited;
            done(std::unexpected(std::move(started.error())));
            return;
        }
        phase = GameServerPhase::Handshaking;
        done(*supervisor->pid());
    }

    bool stop(std::chrono::milliseconds grace) {
        switch (phase) {
            case GameServerPhase::Idle:
            case GameServerPhase::Exited: return false;
            case GameServerPhase::Stopping: return true;
            case GameServerPhase::Preparing: {
                phase = GameServerPhase::Exited;
                strand.post([token = alive.token(), done = std::move(spawned)]() mutable {
                    if (!token.cancelled())
                        done(std::unexpected(error_of({.code = GameServerErrorCode::StoppedBeforeStart})));
                });
                return true;
            }
            case GameServerPhase::Handshaking:
                phase = GameServerPhase::Stopping;
                stop_requested = true;
                supervisor->stop(default_deadline(OpKind::GracefulStop));
                return true;
            case GameServerPhase::AwaitingListen:
            case GameServerPhase::Listening:
                phase = GameServerPhase::Stopping;
                stop_requested = true;
                supervisor->command(gs::Shutdown{.grace_ms = clamp_u32(grace.count())}, [](Result<void>) {});
                grace_timer = timers.after(grace, [this] { supervisor->stop(default_deadline(OpKind::GracefulStop)); });
                return true;
        }
        return false;
    }

    Result<void> request(OperatorCommand command, UniqueFunction<void(CommandResult)> done) {
        if (phase != GameServerPhase::AwaitingListen && phase != GameServerPhase::Listening)
            return std::unexpected(error_of({.code = GameServerErrorCode::NotRunning}));
        const std::string name(command_name(command));
        if (!is_declared(command, binary.description.capabilities))
            return std::unexpected(error_of({.code = GameServerErrorCode::CommandNotDeclared, .command = name}));
        if (Result<void> valid = check_command(command); !valid) return valid;

        const u64 id = ++last_command;
        PendingCommand& pending = commands[id];
        pending.name = name;
        pending.done = std::move(done);
        pending.timeout = timers.after(kOperatorCommandDeadline, [this, id] { on_command_timeout(id); });

        UniqueFunction<void(Result<void>)> answered = [held = supervised.get(), id](Result<void> result) mutable {
            held->forward([&](Impl& impl) { impl.on_answer(id, std::move(result)); });
        };
        std::visit(
            [&]<class C>([[maybe_unused]] C& operator_command) {
                if constexpr (std::is_same_v<C, StartMatch>)
                    supervisor->command(gs::StartMatch{.countdown_s = clamp_u32(operator_command.countdown.count())},
                                        std::move(answered));
                else if constexpr (std::is_same_v<C, EndMatch>)
                    supervisor->command(gs::EndMatch{}, std::move(answered));
                else if constexpr (std::is_same_v<C, ResetMatch>)
                    supervisor->command(gs::Reset{}, std::move(answered));
                else if constexpr (std::is_same_v<C, Kick>)
                    supervisor->command(gs::Kick{.player_id = operator_command.player_id,
                                                 .reason = std::move(operator_command.reason)},
                                        std::move(answered));
                else if constexpr (std::is_same_v<C, SetBans>)
                    supervisor->command(gs::SetBans{.bans = std::move(operator_command.bans)}, std::move(answered));
                else if constexpr (std::is_same_v<C, SetOperators>)
                    supervisor->command(gs::SetOperators{.ip_cidrs = std::move(operator_command.ip_cidrs)},
                                        std::move(answered));
                else if constexpr (std::is_same_v<C, RunCommand>)
                    supervisor->command(gs::RunCommand{.text = std::move(operator_command.text)}, std::move(answered));
                else
                    supervisor->command(gs::Drain{}, std::move(answered));
            },
            command);
        return {};
    }

    void on_answer(u64 id, Result<void> result) {
        const auto found = commands.find(id);
        if (found == commands.end()) return;
        PendingCommand pending = std::move(found->second);
        commands.erase(found);
        if (result) return pending.done(CommandResult{});
        Diagnostic& error = result.error();
        if (error.is(kChildRequestUnsupported)) return pending.done(CommandResult{CommandStatus::Unsupported, std::nullopt});
        if (error.is(kChildGone) || error.is(kChildNotRunning))
            return pending.done(CommandResult{
                CommandStatus::Failed, error_of({.code = GameServerErrorCode::NotRunning, .cause = std::move(error)})});
        pending.done(CommandResult{CommandStatus::Failed, std::move(error)});
    }

    void on_command_timeout(u64 id) {
        const auto found = commands.find(id);
        if (found == commands.end()) return;
        PendingCommand pending = std::move(found->second);
        commands.erase(found);
        pending.done(CommandResult{CommandStatus::Failed,
                                   error_of({.code = GameServerErrorCode::CommandTimeout,
                                             .timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 kOperatorCommandDeadline),
                                             .command = pending.name})});
    }

    // The no-fallback rule: anything but the exact block stops the server at once.
    void fail_listen(ListenFailed failure) {
        phase = GameServerPhase::Stopping;
        stop_requested = true;
        grace_timer.cancel();
        supervisor->stop(std::chrono::milliseconds{0});
        emit(failure);
    }

    [[nodiscard]] std::optional<ListenFailed> listen_mismatch(const std::vector<BoundSocket>& reported) const {
        const std::vector<Port>& requested = config.listen.ports;
        const std::vector<SocketRole> roles = socket_roles(binary.description);
        const std::size_t count = std::max(requested.size(), reported.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (i >= requested.size())
                return ListenFailed{.port = Port{reported[i].port}, .stage = ListenStage::Bind};
            if (i >= reported.size()) return ListenFailed{.port = requested[i], .stage = ListenStage::Bind};
            if (reported[i].port != requested[i].value)
                return ListenFailed{
                    .port = requested[i], .stage = ListenStage::Bind, .bound_instead = Port{reported[i].port}};
            if (i >= roles.size() || reported[i].role != roles[i])
                return ListenFailed{.port = requested[i], .stage = ListenStage::Bind};
        }
        return std::nullopt;
    }

    [[nodiscard]] bool awaiting_or_listening() const noexcept {
        return phase == GameServerPhase::AwaitingListen || phase == GameServerPhase::Listening;
    }

    template <ContractMessage T>
    [[nodiscard]] std::optional<T> decoded(const RawFrame& frame) {
        Result<T> message = decode_contract<T>(frame.payload);
        if (message && known(*message)) return std::move(*message);
        REBOOT_LOG_AT(LogLevel::Warn, Host, std::optional<SessionId>(session), "game server sent a malformed frame {}: {}",
                      frame.type, message ? std::string_view("unknown enum value") : std::string_view(message.error().id));
        return std::nullopt;
    }

    void on_running(u32) {
        if (phase == GameServerPhase::Handshaking) phase = GameServerPhase::AwaitingListen;
    }

    void on_event(const RawFrame& frame) {
        if (is_frame<gs::Listening>(frame)) {
            const std::optional<gs::Listening> listening = decoded<gs::Listening>(frame);
            if (!listening || !awaiting_or_listening()) return;
            if (std::optional<ListenFailed> mismatch = listen_mismatch(listening->bound))
                return fail_listen(std::move(*mismatch));
            if (phase == GameServerPhase::Listening) return;
            phase = GameServerPhase::Listening;
            bound = listening->bound;
            emit(Listening{bound});
        } else if (is_frame<gs::ListenFailed>(frame)) {
            const std::optional<gs::ListenFailed> failed = decoded<gs::ListenFailed>(frame);
            if (!failed || !awaiting_or_listening()) return;
            ListenFailed failure{.port = Port{failed->port}, .stage = failed->stage};
            if (failed->os_error != 0) failure.os_error = SystemError{SystemError::Origin::Host, failed->os_error};
            fail_listen(std::move(failure));
        } else if (is_frame<gs::StateChanged>(frame)) {
            if (const auto changed = decoded<gs::StateChanged>(frame)) emit(MatchStateChanged{changed->state});
        } else if (is_frame<gs::PlayerJoined>(frame)) {
            const std::optional<gs::PlayerJoined> joined = decoded<gs::PlayerJoined>(frame);
            if (!joined) return;
            Player player{joined->player_id, joined->account_id, joined->display_name, joined->address};
            if (!emit(PlayerJoined{player})) return;
            const auto existing = std::ranges::find(players, player.player_id, &Player::player_id);
            if (existing != players.end()) *existing = std::move(player);
            else players.push_back(std::move(player));
        } else if (is_frame<gs::PlayerLeft>(frame)) {
            const std::optional<gs::PlayerLeft> left = decoded<gs::PlayerLeft>(frame);
            if (!left) return;
            const auto existing = std::ranges::find(players, left->player_id, &Player::player_id);
            if (existing == players.end()) return;
            const u32 player_id = existing->player_id;
            if (!emit(PlayerLeft{*existing})) return;
            std::erase_if(players, [&](const Player& player) { return player.player_id == player_id; });
        } else if (is_frame<gs::PlayerCount>(frame)) {
            if (const auto count = decoded<gs::PlayerCount>(frame)) emit(PlayerCountChanged{count->n});
        } else if (is_frame<gs::MatchEnded>(frame)) {
            if (auto ended = decoded<gs::MatchEnded>(frame))
                emit(MatchEnded{ended->reason, std::move(ended->winner), std::move(ended->placements)});
        } else if (is_frame<gs::Fatal>(frame)) {
            if (auto fatal = decoded<gs::Fatal>(frame))
                emit(ServerFatal{std::move(fatal->code), std::move(fatal->detail)});
        } else {
            REBOOT_LOG_AT(LogLevel::Debug, Host, std::optional<SessionId>(session),
                          "game server sent unknown frame {}", frame.type);
        }
    }

    void on_unresponsive(u32 missed) { emit(ServerUnresponsive{static_cast<int>(missed)}); }

    void on_exit(const process::ChildExitInfo& exit) {
        phase = GameServerPhase::Exited;
        grace_timer.cancel();
        const CancelToken token = alive.token();
        std::map<u64, PendingCommand> unanswered = std::move(commands);
        commands.clear();
        for (auto& [id, pending] : unanswered) {
            pending.done(CommandResult{CommandStatus::Failed, error_of({.code = GameServerErrorCode::NotRunning})});
            if (token.cancelled()) return;
        }
        ServerExited exited{.cause = stop_requested ? process::ChildExitCause::Requested : exit.cause,
                            .status = exit.status,
                            .error = stop_requested ? std::nullopt : exit.error};
        if (!emit(exited)) return;
        players.clear();
    }

    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
    SessionId session;
    DescribedBinary binary;
    GameServerConfig config;
    NativePath session_dir;
    GameServerEventSink sink;

    GameServerPhase phase = GameServerPhase::Idle;
    // Set by stop() and a failed listen: the exit is then reported as Requested.
    bool stop_requested = false;
    UniqueFunction<void(Result<u32>)> spawned;
    std::vector<BoundSocket> bound;
    std::vector<Player> players;
    u64 last_command = 0;
    std::map<u64, PendingCommand> commands;
    TimerHandle grace_timer;
    CancelSource alive;
    std::unique_ptr<Supervised> supervised;
    process::ChildSupervisor* supervisor = nullptr;
};

GameServerProcess::GameServerProcess(ports::IProcessLauncher& launcher, ports::IFileSystem& fs, WorkerPool& workers,
                                     Executor& strand, TimerService& timers, const IClock& clock,
                                     const AppLayout& layout, GameServerLaunch launch, GameServerEventSink on_event,
                                     process::ChildRecordCallback record)
    : impl_(std::make_unique<Impl>(launcher, fs, workers, strand, timers, clock, layout, std::move(launch),
                                   std::move(on_event), std::move(record))) {}

GameServerProcess::~GameServerProcess() = default;

Result<void> GameServerProcess::start(UniqueFunction<void(Result<u32>)> spawned) {
    return impl_->start(std::move(spawned));
}

bool GameServerProcess::stop(std::chrono::milliseconds grace) { return impl_->stop(grace); }

Result<void> GameServerProcess::request(OperatorCommand command, UniqueFunction<void(CommandResult)> done) {
    return impl_->request(std::move(command), std::move(done));
}

const SessionId& GameServerProcess::session() const noexcept { return impl_->session; }

GameServerPhase GameServerProcess::phase() const noexcept { return impl_->phase; }

std::optional<u32> GameServerProcess::pid() const noexcept { return impl_->supervisor->pid(); }

const DescribedBinary& GameServerProcess::binary() const noexcept { return impl_->binary; }

std::span<const BoundSocket> GameServerProcess::bound() const noexcept { return impl_->bound; }

std::span<const Player> GameServerProcess::players() const noexcept { return impl_->players; }

}  // namespace rb::gameserver
