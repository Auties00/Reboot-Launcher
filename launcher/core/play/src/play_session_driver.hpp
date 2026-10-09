#pragma once

#include <memory>
#include <optional>

#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/client_dll_peer.hpp"
#include "reboot/play/play_session_state.hpp"
#include "reboot/play/preflight.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/sessions/session_driver.hpp"

namespace reboot::play {

class MatchTargets;
class PlayEnv;

// What a driver releases at its end; it never stops the linked auto-server, a registry child.
// The front's route and fixed listeners go through `env`.
struct PlaySessionReleases {
    MatchTargets& match_targets;
    PlayEnv& env;
};

// How the driver reaches the PlayService that opened it; both run on the strand.
struct PlaySessionHooks {
    // A stop began, before anything is stopped: the service ends the start's pending steps.
    UniqueFunction<void(const sessions::StopRequest&)> on_stop;
    // The registry destroyed the driver; never called from inside stop().
    UniqueFunction<void()> on_destroyed;
};

// Strand-only. The registry's handle on one play session, which owns what start() took for it.
// Self-ends reach the registry as report_exit: our DLL's peer lost is Fatal, unresponsive is
// Unresponsive, a HostFatal is Fatal. A linked auto-server that ends first only raises
// play.linked_server_ended as a degraded condition.
class PlaySessionDriver final : public sessions::ISessionDriver {
public:
    PlaySessionDriver(PlaySessionReleases releases, PlaySessionHooks hooks);
    // Withdraws the match target, removes the route and fixed listeners, then drops the peer
    // (revoking its token), the game session and the preflight pins and lease.
    ~PlaySessionDriver() override;
    PlaySessionDriver(const PlaySessionDriver&) = delete;
    PlaySessionDriver& operator=(const PlaySessionDriver&) = delete;

    // The id SessionRegistry::open gave the session; the releases name it.
    void bind(SessionId session);

    void adopt(Preflight preflight);
    void adopt(std::unique_ptr<game_channel::ClientDllPeer> peer);
    void adopt(std::unique_ptr<ports::IGameSession> game);

    [[nodiscard]] PlaySessionState& state() noexcept { return state_; }
    [[nodiscard]] Preflight* preflight() noexcept { return preflight_ ? &*preflight_ : nullptr; }
    [[nodiscard]] ports::IGameSession* game() noexcept { return game_.get(); }

    // Shutdown to our DLL, then IGameSession::stop(request.grace); `done` once the game exited.
    void stop(const sessions::StopRequest& request, sessions::StopDone done) override;
    // Completes a pending stop.
    void on_game_exited();
    [[nodiscard]] bool game_exited() const noexcept { return game_exited_; }

    // The service is going away: everything is released now, and a later stop() ends at once.
    void detach();

private:
    void release();

    PlaySessionReleases releases_;
    PlaySessionHooks hooks_;
    std::optional<SessionId> session_;
    PlaySessionState state_;
    std::optional<Preflight> preflight_;
    std::unique_ptr<game_channel::ClientDllPeer> peer_;
    std::unique_ptr<ports::IGameSession> game_;
    std::optional<sessions::StopDone> pending_stop_;
    bool game_exited_ = false;
    bool released_ = false;
};

}  // namespace reboot::play
