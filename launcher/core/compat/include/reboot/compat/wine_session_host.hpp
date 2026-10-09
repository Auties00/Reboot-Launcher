#pragma once

#include <memory>
#include <string_view>

#include "reboot/compat/path_mapper.hpp"
#include "reboot/compat/runner_profile.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/runner.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/process/env_builder.hpp"

namespace reboot {
class Executor;
class TimerService;
}  // namespace reboot

namespace reboot::ports {
class IProcessLauncher;
}

namespace reboot::game_channel {
class GameChannelListener;
}

namespace reboot::compat {

// One line of the runner's own output; the engine logs it as LogCategory::Wine under the
// session, which routes it to WineLogSink.
using WineLogLine = UniqueFunction<void(SessionId session, std::string_view line)>;

struct WineSessionHostDeps {
    ports::IProcessLauncher& processes;
    ports::IRunnerPlatform& runner;
    game_channel::GameChannelListener& channel;
    TimerService& timers;
    Executor& strand;
    WineLogLine wine_log;
};

// What one session runs under, from its PreparedRuntime and PreparedPrefix.
struct WineSessionSetup {
    RunnerKind kind{};
    ports::RuntimeLayout layout;
    NativePath prefix;
    PathMapper paths;
    // reboot-winhost.exe from the session's pinned payload.
    NativePath winhost_exe;
    // Daemon base, client and play_settings(log_dir); the host adds the runner and channel layers.
    process::EnvBuilder env;
    // Umu exposes it to pressure-vessel with the build, the injected DLLs and winhost; the
    // Proton log goes here under Verbose Wine logging.
    NativePath log_dir;
};

// Covers no capability ids (decisions dll-injection-strategy, linux-compat-layer, macos-compat-layer,
// linux-gui-sandbox-distribution, owner-2).
// Strand-only. ISessionHost for play on macOS and Linux; hosting never uses it. launch() consumes
// the staged setup:
// - opens a winhost peer, whose REBOOT_CTL and REBOOT_CTL_TOKEN go in winhost's channel layer;
// - runs IRunnerPlatform::runner_launch of reboot-winhost.exe in scope reboot-session-<id>;
// - answers winhost's Hello with SpawnGame in UTF-16 through PathMapper, park paths included,
//   so winhost parks them inside the prefix. Its environment block
//   holds only the game's channel layer: winhost lays it over the environment Wine gave winhost,
//   so the prefix's SystemRoot, windir, USERPROFILE, APPDATA and LOCALAPPDATA reach the game;
// - relays winhost's events; WhFatal becomes HostFatal. Runner output goes to WineLogLine.
// winhost EOF or the runner exiting ends the session with Exited{Winhost}, preceded by HostFatal
// unless Exited{Game} came first or stop() was called. resume() and inject() wait for the
// Welcome; stop(grace) sends Stop and, past the grace, drops the peer, which ends winhost's Job,
// and kills the runner tree. `on_event` is always posted to the strand, never called from inside
// launch().
class WineSessionHost final : public ports::ISessionHost {
public:
    explicit WineSessionHost(WineSessionHostDeps deps);
    ~WineSessionHost() override;
    WineSessionHost(const WineSessionHost&) = delete;
    WineSessionHost& operator=(const WineSessionHost&) = delete;

    // compat.session_already_staged.
    Result<void> stage(SessionId session, WineSessionSetup setup);
    // For a session that will not launch after all.
    void discard(SessionId session) noexcept;

    // compat.session_not_staged, compat.path_not_mapped (or path_not_utf8), compat.path_not_exposable
    // under Umu, compat.runner_spawn_failed, or the game_channel error of opening the peer.
    Result<std::unique_ptr<ports::IGameSession>> launch(const ports::SessionLaunch& launch,
                                                        UniqueFunction<void(ports::SessionHostEvent)> on_event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::compat
