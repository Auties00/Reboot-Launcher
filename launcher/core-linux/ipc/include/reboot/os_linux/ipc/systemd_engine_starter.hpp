#pragma once

#include <chrono>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/os_linux/ipc/linux_client_paths.hpp"
#include "reboot/ports/ipc.hpp"

namespace reboot::os_linux::ipc {

// Covers no capability ids; IEngineStarter for reboot_client's Autostart mode.
class SystemdEngineStarter final : public ports::IEngineStarter {
public:
    // Each systemctl or systemd-run child is SIGKILLed past this, so a hung user bus cannot hold
    // spawn.lock for the whole connect deadline.
    static constexpr std::chrono::seconds kSystemdCallDeadline{3};

    // `caller` is the context captured once at rb_ctx_create. `paths` gives the runtime base,
    // the XDG homes, the passwd fields and the AppImage. `under_steam_reaper`: an ancestor is
    // Steam's reaper, whose teardown would kill a self-spawned engine and every host in it.
    SystemdEngineStarter(ports::CallerContext caller, LinuxClientPaths paths, bool under_steam_reaper)
        : caller_(std::move(caller)), paths_(std::move(paths)), under_steam_reaper_(under_steam_reaper) {}

    // An elevated caller gets ElevatedRefused before anything starts. Otherwise the missing
    // directories up to <root>/state are created 0700, since the first autostart precedes any
    // engine, and the call holds <root>/state/spawn.lock (opened O_CLOEXEC, F_OFD_SETLKW) for its
    // duration only. AlreadyRunning at once when F_OFD_GETLK finds state/engine.lock held: the
    // engine takes it as an OFD lock, which flock does not see. Then the first step that applies;
    // a systemctl or systemd-run call that cannot run, exits non-zero or times out moves on.
    // 1. Only for the default root, whose socket the installed units serve, and a runtime base
    //    from $XDG_RUNTIME_DIR, their %t. `systemctl --user show
    //    --property=LoadState,ActiveState,Listen reboot-engine.socket` must report the unit
    //    loaded and listening on exactly the client's socket path, which a changed XDG_DATA_HOME
    //    breaks. Then `systemctl --user restart reboot-engine.socket` when the unit is active but
    //    no socket sits at the path (a foreground engine bound its own there and unlinked it on
    //    exit), `start` otherwise. Started, and systemd spawns the engine on the client's connect.
    // 2. Only for a runtime base from $XDG_RUNTIME_DIR, since the user manager's engine listens
    //    under its own. AlreadyRunning when reboot-engine-<hash16>.service is active, activating
    //    or reloading. Otherwise `systemd-run --user --unit=reboot-engine-<hash16> --collect
    //    --quiet` running the engine with run --origin=on-demand and --setenv for XDG_DATA_HOME,
    //    XDG_CACHE_HOME and XDG_STATE_HOME as `paths` resolved them (the user manager's
    //    environment often lacks them) and REBOOT_LAUNCHER_HOME=<root> when overridden. Started.
    // 3. ConnectOnly under a Steam reaper and NoInteractiveSession for a non-interactive caller;
    //    steps 1 and 2 serve both, since the engine they start is the user manager's child.
    //    Otherwise a setsid double fork whose grandchild resets every signal disposition and the
    //    signal mask (an ignored signal survives exec), closes every fd >= 3 except the exec-error
    //    pipe (SYS_close_range, else a close loop up to RLIMIT_NOFILE: glibc 2.28 has no
    //    closefrom), puts stdio on /dev/null, enters the exe's directory and execs the engine with
    //    run --origin=on-demand and a fixed environment: HOME, USER, LOGNAME and SHELL from the
    //    passwd entry; the step 2 variables; and only PATH (/usr/local/bin:/usr/bin:/bin when
    //    unset), TMPDIR, XDG_CONFIG_HOME and XDG_RUNTIME_DIR when absolute, XDG_DATA_DIRS,
    //    XDG_CONFIG_DIRS, DBUS_SESSION_BUS_ADDRESS (libsecret), LANG and LC_* from this process.
    //    argv, envp and the fd limit are built before fork, so the children make only
    //    async-signal-safe calls. An exec errno travels back over the CLOEXEC pipe; any failure is
    //    platform.ipc_engine_spawn_failed. Started.
    // Steps 2 and 3 run `<appimage> engine run ...` from an AppImage, since the mount behind
    // engine_exe goes away when the client's AppImage exits.
    Result<ports::StartResult> ensure_started(const NativePath& engine_exe, const DataRoot& root) override;

private:
    ports::CallerContext caller_;
    LinuxClientPaths paths_;
    bool under_steam_reaper_;
};

}  // namespace reboot::os_linux::ipc
