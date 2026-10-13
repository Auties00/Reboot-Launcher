#pragma once

#include <chrono>
#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_macos::ipc {

// Covers no capability ids; IEngineStarter for reboot_client's Autostart mode. The engine is
// always launchd's child, never the client's.
class SmAppServiceEngineStarter final : public ports::IEngineStarter {
public:
    // The /bin/launchctl child is SIGKILLed past this, so a hung launchd cannot hold spawn.lock
    // for the whole connect deadline.
    static constexpr std::chrono::seconds kLaunchctlDeadline{3};
    // SMAppService register has no cancellable form, so it is only awaited this long.
    static constexpr std::chrono::seconds kAgentRegisterDeadline{3};
    // Between kickstarts of a label launchd has not loaded yet, right after registering it.
    static constexpr std::chrono::milliseconds kKickstartRetryInterval{100};

    // `uid` names the gui/<uid> launchd domain; `caller` is the context captured once at
    // rb_ctx_create; `translocated` is MacClientPaths::translocated().
    SmAppServiceEngineStarter(u32 uid, ports::CallerContext caller, bool translocated);
    // Joins a register still in flight.
    ~SmAppServiceEngineStarter() override;
    SmAppServiceEngineStarter(const SmAppServiceEngineStarter&) = delete;
    SmAppServiceEngineStarter& operator=(const SmAppServiceEngineStarter&) = delete;

    // Before anything starts: ElevatedRefused for an elevated caller, NoInteractiveSession
    // outside the user's Aqua session (SSH included), and CannotDetach for an overridden root
    // (REBOOT_LAUNCHER_HOME), which the one agent cannot serve; IpcClient then points the user
    // at reboot-engine run --foreground. For the default root:
    // 1. Only when this process's main bundle carries kEngineAgentPlist (the app; the CLI skips
    //    this step), before spawn.lock is taken since registering is idempotent: the
    //    SMAppService agent for that plist. NotRegistered (or NotFound) is registered on a thread
    //    this starter owns, which catches everything and reports internal.bug. Past
    //    kAgentRegisterDeadline the call is platform.agent_register_timed_out (retryable), and the
    //    next call awaits the same register instead of issuing another. A register error is
    //    platform.agent_register_failed unless the agent ended up enabled. RequiresApproval,
    //    before or after registering, gives AwaitingUser. A translocated app registers nothing,
    //    since launchd would keep its random read-only mount: an agent not yet registered fails
    //    with platform.app_translocated (Conflict), telling the user to move the app.
    // 2. Holding <root>/state/spawn.lock (flock, for this call only): posix_spawn of
    //    /bin/launchctl kickstart gui/<uid>/<kEngineAgentLabel> with an empty environment and
    //    stdio on /dev/null, its exit awaited through kqueue EVFILT_PROC, which reports the
    //    status even when the host ignores SIGCHLD. Exit 0 gives Started (kickstart leaves a
    //    running engine alone). A label unknown to the domain, because the app never registered
    //    it, or disabled in Login Items, gives AwaitingUser; when this call has just registered
    //    it, the kickstart is retried every kKickstartRetryInterval first, since launchd may load
    //    the job late. Past kLaunchctlDeadline, for all tries together, the child is SIGKILLed
    //    and reaped: platform.agent_kickstart_timed_out (retryable). Anything else is
    //    platform.agent_kickstart_failed.
    // Both AwaitingUser cases reach IpcClient alike, since ports::StartResult carries no reason.
    Result<ports::StartResult> ensure_started(const NativePath& engine_exe, const DataRoot& root) override;

private:
    struct PendingRegister;

    u32 uid_;
    ports::CallerContext caller_;
    bool translocated_ = false;
    std::unique_ptr<PendingRegister> pending_register_;
};

}  // namespace rb::os_macos::ipc
