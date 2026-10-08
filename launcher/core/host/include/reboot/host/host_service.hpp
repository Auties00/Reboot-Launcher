#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/command_result.hpp"
#include "reboot/gameserver/game_server_binary.hpp"
#include "reboot/host/host_command.hpp"
#include "reboot/host/host_event.hpp"
#include "reboot/host/host_phase.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/host/host_snapshot.hpp"
#include "reboot/host/host_start_request.hpp"
#include "reboot/host/readiness_policy.hpp"
#include "reboot/process/child_record.hpp"
#include "reboot/publish/share_link.hpp"
#include "reboot/sessions/shutdown_cause.hpp"

namespace reboot {
class AppLayout;
class EventBus;
class Executor;
class IClock;
class TimerService;
class UserRequestRegistry;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
class IProcessLauncher;
}  // namespace reboot::ports

namespace reboot::builds {
class Library;
}

namespace reboot::identity {
class IdentityService;
}

namespace reboot::net {
class PortMapperService;
class PortOwnerService;
class UdpBeaconProber;
}  // namespace reboot::net

namespace reboot::publish {
class HostIdentityStore;
class HostPublisher;
}  // namespace reboot::publish

namespace reboot::sessions {
class SessionRegistry;
}

namespace reboot::storage {
class Settings;
}

namespace reboot::support {
class SupportPolicy;
}

namespace reboot::host {

class HostPortAllocator;
class HostProfileStore;
class IHostBackendLink;

// Matches the rbsb edge's cap of 4 hosts per public IP.
inline constexpr u32 kDefaultHostLimit = 4;

// host may not depend on secrets, so the engine reads the HostJoinPassword secret of a profile
// through this. Only secrets.not_found becomes nullopt (no password); any other error, such as
// secrets.not_ready, fails the start, so a protected profile is never published open.
using JoinPasswordSource = UniqueFunction<Result<std::optional<SecretString>>(const HostProfileId&)>;

struct HostServiceOptions {
    u32 host_limit = kDefaultHostLimit;
    ReadinessPolicy readiness;
    // Between Shutdown{grace} and the kill, for a stop and for a respawn.
    std::chrono::milliseconds stop_grace = default_deadline(OpKind::GracefulStop);
};

struct HostServiceDeps {
    HostProfileStore& profiles;
    sessions::SessionRegistry& sessions;
    gameserver::GameServerBinary& binary;
    const support::SupportPolicy& support;
    builds::Library& library;
    const identity::IdentityService& identity;
    const storage::Settings& settings;
    HostPortAllocator& allocator;
    net::PortOwnerService& port_owners;
    net::UdpBeaconProber& prober;
    net::PortMapperService& mapper;
    publish::HostPublisher& publisher;
    publish::HostIdentityStore& identities;
    IHostBackendLink& backend;
    ports::IProcessLauncher& launcher;
    ports::IFileSystem& fs;
    WorkerPool& workers;
    Executor& strand;
    TimerService& timers;
    const IClock& clock;
    OpRegistry& ops;
    EventBus& events;
    UserRequestRegistry& requests;
    const AppLayout& layout;
    JoinPasswordSource join_password;
    gameserver::BaseEnvSource base_env;
    process::ChildRecordCallback record;
    HostServiceOptions options = {};
};

// Capabilities: hosting.auto-restart, hosting.+8, hosting.reachability-check, hosting.game-server-port,
// settings-storage.hosting-store, game-launch.linked-server, game-launch.+9.
// Strand-only. Hosts with our native reboot-game-server: no Wine, no prefix, no injection. Each
// host session pins its profile, build, settings snapshot (host.update_policy among them) and
// game-server binary at start, so a restart never re-reads them; update_profile() and
// refresh_join_password() change only what they name.
//
// start() checks synchronously: the profile exists; check_start() (the auto profile exactly with
// linked_to, never Listed); the profile has no live or starting session (host.profile_busy),
// except the auto profile, whose second concurrent session runs unpublished because its rbsb
// identity is held; fewer than host_limit host sessions exist (host.host_limit_reached); a target:
// the override's or the profile's build, else the profile's version, else the library's host
// selection (host.no_build_selected), where a build needs a confirmed version
// (host.build_version_unknown). A version alone hosts with no game files, so GameTarget::build_root
// stays unset. The op (OpKind::Host) then runs:
// 1. Describe the game server (cached by sha256) and rate the build with the SupportPolicy host
//    cell: Blocked fails, Untested raises ConfirmUntested unless already confirmed.
// 2. Read the join password; open the session in the registry, with linked_to as its parent;
//    lease the backend when the description needs one.
// 3. Reserve a block sized by the description's sockets through HostPortAllocator.
// 4. Spawn the server with GameServerConfig (block, match, operators, active bans) and wait for
//    Listening, within ReadinessPolicy::deadline of the spawn. On ListenFailed, Auto reserves the
//    next block above and respawns; Pinned fails. The op completes with the SessionId at
//    Listening, after HostListening was published; listening() answers from then on.
// 5. Readiness, in order: IPortInspector confirms the server's pid holds every port, then one
//    loopback rbsb probe of the game port, within what is left of the same deadline. On timeout
//    the session stays up as LiveUnpublished (or stops, by ReadinessPolicy::on_timeout); a late
//    answer still continues. Then port mapping of the whole block when the profile asks for it,
//    and publish with game_port = the granted external port, or the bound one. Neither failure
//    stops the session.
// Any failure before Listening, a listen timeout included, stops the session with LaunchFailed
// and fails the op; nothing is left half started.
//
// On MatchEnded, MatchEndPolicy runs once per match on a cancellable Restarting timer (duplicates
// are ignored), announced by a MatchEvent carrying the action and when it fires: Restart resets
// in-process when the server supports it, falling back to a respawn on the same block; Shutdown
// stops the session with sessions::StopReason::MatchEnded; None leaves it in its lobby. The rbsb
// entry stays registered and hidden while Restarting, and the session keeps its id and parent. A
// stop during Restarting cancels the timer and any respawn and never relaunches. A server that
// exits on its own, hangs or reports Fatal ends the session; an earlier incarnation's late exit
// is ignored.
class HostService {
public:
    explicit HostService(HostServiceDeps deps);
    ~HostService();
    HostService(const HostService&) = delete;
    HostService& operator=(const HostService&) = delete;

    // Every profile write below publishes HostProfilesChanged.
    [[nodiscard]] std::vector<HostProfile> profiles() const;
    Result<HostProfile> create_profile(HostProfile draft);
    // Live sessions of the profile get the operator allowlist and bans through the server, and
    // the listing, server name and description through HostPublisher::update. The rest, max
    // players included since the running server enforces its own, applies from the next start.
    Result<HostProfile> update_profile(HostProfile profile);
    // host.profile_busy while the profile has a live session; its rbsb identity goes with it.
    Result<void> delete_profile(HostProfileId id);
    // ResetHooks::reset_records for ResetGroup::Host: HostProfileStore::reset, then each live
    // session's new listing, server name and description are published as in update_profile.
    Result<void> reset_profiles();
    // The engine calls this after the profile's HostJoinPassword secret changed; live sessions
    // publish the new password, or none. A read error leaves the published password unchanged.
    Result<void> refresh_join_password(HostProfileId profile);

    Result<OpHandle> start(HostStartRequest request);

    // Operator pass-through to a live session's server; host.server_not_running while Restarting or
    // without a process. `done` runs on the strand exactly once.
    Result<void> command(SessionId session, HostCommand command,
                         UniqueFunction<void(gameserver::CommandResult)> done);

    // Stops a pending match-end action and leaves the server running; false when none is pending.
    Result<bool> cancel_match_end(SessionId session);

    // The block and bound sockets of the session's current server, as last published in
    // HostListing; host.not_listening before Listening and during a respawn.
    [[nodiscard]] Result<HostListening> listening(SessionId session) const;

    // reboot://<server id> of a published session; publish.not_published otherwise.
    [[nodiscard]] Result<publish::ShareLink> share_link(SessionId session) const;

    [[nodiscard]] Result<HostPhase> phase(SessionId session) const;

    // Host.status; host.not_host_session for a session that is not a live host session.
    [[nodiscard]] Result<HostSnapshot> status(SessionId session) const;

    // Engine.drain and the shutdown DrainHosts step. DrainUpdate follows each session's pinned
    // update policy, the profile's or else host.update_policy: AfterMatch sends Drain, hides the
    // entry (Draining) and stops the session with Update at its next MatchEnded, or at once with
    // no player connected; Manual leaves it running until the user stops it. Any other cause stops
    // every host session now. `done` runs on the strand once every session it stops has ended.
    void drain(sessions::ShutdownCause cause, UniqueFunction<void()> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::host
