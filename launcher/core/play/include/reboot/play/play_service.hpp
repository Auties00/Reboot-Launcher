#pragma once

#include <memory>
#include <optional>
#include <variant>

#include "reboot/backend/login_observed_event.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/play/display_context.hpp"
#include "reboot/play/play_plan.hpp"
#include "reboot/play/play_request.hpp"
#include "reboot/play/play_session_state.hpp"
#include "reboot/ports/process.hpp"

namespace reboot {
class EventBus;
class Executor;
class IRandom;
class Redactor;
class TimerService;
class UserRequestRegistry;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class ISessionHost;
class ISystemInfo;
}  // namespace reboot::ports

namespace reboot::storage {
class Settings;
}

namespace reboot::builds {
class Library;
}

namespace reboot::catalog {
class CatalogService;
}

namespace reboot::support {
class SupportPolicy;
}

namespace reboot::components {
class ComponentStore;
}

namespace reboot::compat {
class PrefixManager;
class RuntimeService;
class WineSessionHost;
}  // namespace reboot::compat

namespace reboot::identity {
class IdentityService;
}

namespace reboot::secrets {
class SecretService;
}

namespace reboot::backend {
class BackendService;
class RemoteLogin;
}  // namespace reboot::backend

namespace reboot::front {
class LegacyFixedListeners;
class SessionFront;
}  // namespace reboot::front

namespace reboot::game_channel {
class GameChannelListener;
}

namespace reboot::sessions {
class SessionRegistry;
}

namespace reboot::browser {
class GameServerTarget;
class JoinService;
}  // namespace reboot::browser

namespace reboot::host {
class HostService;
}

namespace reboot::play {

class MatchTargets;

// Windows: the game runs natively under Win32SessionHost.
struct NativeRunner {
    ports::ISessionHost& host;
};

// macOS and Linux: the game runs under the launcher's Wine through winhost.
struct WineRunner {
    compat::RuntimeService& runtime;
    compat::PrefixManager& prefixes;
    compat::WineSessionHost& host;
};

using PlayRunner = std::variant<NativeRunner, WineRunner>;

struct PlayServiceDeps {
    storage::Settings& settings;
    builds::Library& library;
    const catalog::CatalogService& catalog;
    const support::SupportPolicy& support;
    components::ComponentStore& components;
    PlayRunner runner;
    identity::IdentityService& identity;
    secrets::SecretService& secrets;
    backend::BackendService& backend;
    backend::RemoteLogin& remote_login;
    front::SessionFront& front;
    front::LegacyFixedListeners& legacy_listeners;
    game_channel::GameChannelListener& channel;
    sessions::SessionRegistry& sessions;
    browser::JoinService& join;
    browser::GameServerTarget& addresses;
    host::HostService& hosts;
    MatchTargets& match_targets;
    ports::IFileSystem& fs;
    ports::ISystemInfo& system;
    IRandom& random;
    Redactor& redactor;
    UserRequestRegistry& requests;
    OpRegistry& ops;
    EventBus& events;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
};

struct PlayServiceOptions {
    SessionMatch session_match = SessionMatch::OsSession;
    // EnvBuilder's daemon base layer: the user's environment as the platform reports it.
    ports::EnvBlock daemon_env;
};

// Covers game-launch.orchestration, game-launch.arguments, game-launch.+10.
// Strand-only. A session is opened in the SessionRegistry with a PlaySessionDriver, so every end
// goes through SessionRegistry::stop; ShutdownCoordinator uses stop_all(SessionKind::Play).
// One play session at a time, since the backend resolves match targets by account only.
// Credential step: RemoteLogin runs before the spawn for RemotePasswordExchange and
// FrontTicket{SwapForStoredPassword}, and alone raises their NeedsSecret; LegacyArgv uses require().
// LegacyFixed binds injection::legacy_requirements(): 127.0.0.1:3551, plus :80 on Windows (owner decision 5).
// The environment takes the pinned play settings through EnvBuilder::play_settings, with
// WineSessionSetup::log_dir as the Wine log folder.
class PlayService {
public:
    // Registers kCalderaToken with the Redactor, so every log sink masks it.
    PlayService(PlayServiceDeps deps, PlayServiceOptions options);
    ~PlayService();
    PlayService(const PlayService&) = delete;
    PlayService& operator=(const PlayService&) = delete;

    // Play.plan. Fails only for an unknown build; every other problem is a blocker in the plan.
    [[nodiscard]] Result<PlayPlan> plan(const PlayRequest& request) const;

    // Play.start, an OpKind::Play op scaled by the runner multiplier; completes with the SessionId
    // at LoggedIn. A validation failure creates neither op nor session; a later one stops the
    // session with LaunchFailed.
    Result<OpHandle> start(PlayRequest request, DisconnectPolicy policy = DisconnectPolicy::Detached);

    // BackendService's login observer: LoggedIn for a session whose DLL has not reported it.
    void on_login_observed(const backend::LoginObservedEvent& event);

    [[nodiscard]] std::optional<PlaySessionState> state(SessionId session) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::play
