#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/command_result.hpp"
#include "reboot/gameserver/game_server_config.hpp"
#include "reboot/gameserver/game_server_description.hpp"
#include "reboot/gameserver/game_server_event.hpp"
#include "reboot/gameserver/operator_command.hpp"
#include "reboot/process/built_env.hpp"
#include "reboot/process/child_record.hpp"

namespace reboot {
class AppLayout;
class Executor;
class IClock;
class TimerService;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class IProcessLauncher;
}  // namespace reboot::ports

namespace reboot::gameserver {

// AwaitingListen: Welcome was sent. Exited is final; a respawn is a new GameServerProcess.
enum class GameServerPhase : u8 { Idle, Preparing, Handshaking, AwaitingListen, Listening, Stopping, Exited };

struct GameServerLaunch {
    SessionId session;
    // From GameServerBinary::describe(); ServerHello must equal its description.
    DescribedBinary binary;
    GameServerConfig config;
    process::BuiltEnv env;
    // Linux: the per-session systemd user scope, when systemd is available.
    std::optional<std::string> scope_name;
};

// Without a reply by then, request() answers Failed with gameserver.command_timeout.
inline constexpr std::chrono::seconds kOperatorCommandDeadline{10};

// Runs on the strand.
using GameServerEventSink = UniqueFunction<void(const GameServerEvent&)>;

// Capabilities: none; implements owner-2, concurrent-hosts, hosting-port-forwarding (exact bind),
// identity-uniqueness (unverified ids) and game-control-channel (one answer per request).
// Strand-only; one per host session. Runs `<exe> --control=stdio` with cwd and log_dir at
// AppLayout::game_server_session_dir, under a ChildSupervisor with report-only liveness and no
// restart. Answers ServerHello with ServerWelcome{to_wire(config)} only when the Hello equals
// the described binary. A Listening that is not exactly the requested block, or a ListenFailed,
// stops the child with no grace and is reported as ListenFailed.
class GameServerProcess {
public:
    GameServerProcess(ports::IProcessLauncher& launcher, ports::IFileSystem& fs, WorkerPool& workers,
                      Executor& strand, TimerService& timers, const IClock& clock, const AppLayout& layout,
                      GameServerLaunch launch, GameServerEventSink on_event, process::ChildRecordCallback record);
    // Kills a live child at once, with no further events.
    ~GameServerProcess();
    GameServerProcess(const GameServerProcess&) = delete;
    GameServerProcess& operator=(const GameServerProcess&) = delete;

    // From Idle only. Validates the config against the description synchronously, then creates
    // the session folder on the WorkerPool and spawns. On an error return `spawned` is dropped
    // uncalled; otherwise it runs once on the strand with the pid or the spawn error, and after a
    // pid, events follow and ServerExited ends them.
    Result<void> start(UniqueFunction<void(Result<u32>)> spawned);

    // The only way to send Shutdown, so its exit is reported with cause Requested. By phase:
    // - Idle: does nothing and returns false.
    // - Preparing: cancels the spawn, `spawned` gets gameserver.stopped_before_start, and no
    //   events follow.
    // - Handshaking: no channel yet, so closes stdin at once and kills after GracefulStop.
    // - AwaitingListen, Listening: sends Shutdown{grace}, closes stdin after `grace`, then kills
    //   after GracefulStop.
    // - Stopping: keeps the stop already under way.
    // - Exited: returns false.
    bool stop(std::chrono::milliseconds grace);

    // From AwaitingListen or Listening, else gameserver.not_running; an undeclared command fails
    // with gameserver.command_not_declared and is never sent. On an error return `done` is
    // dropped uncalled; otherwise it runs later on the strand exactly once: with the reply, with
    // Failed and gameserver.command_timeout after kOperatorCommandDeadline, or with Failed and
    // gameserver.not_running when the process exits first.
    Result<void> request(OperatorCommand command, UniqueFunction<void(CommandResult)> done);

    [[nodiscard]] const SessionId& session() const noexcept;
    [[nodiscard]] GameServerPhase phase() const noexcept;
    [[nodiscard]] std::optional<u32> pid() const noexcept;
    [[nodiscard]] const DescribedBinary& binary() const noexcept;
    // Empty until Listening.
    [[nodiscard]] std::span<const BoundSocket> bound() const noexcept;
    // Maintained from PlayerJoined and PlayerLeft; Host.status lists it.
    // Changes after the sink has run, so a leaving player is still listed during its PlayerLeft;
    // a PlayerLeft for a player id that never joined is dropped.
    [[nodiscard]] std::span<const Player> players() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::gameserver
