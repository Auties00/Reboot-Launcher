#pragma once

#include <memory>
#include <optional>

#include "reboot/foundation/types.hpp"
#include "reboot/game_channel/client_dll_peer.hpp"
#include "reboot/play/play_session_state.hpp"
#include "reboot/play/preflight.hpp"
#include "reboot/ports/session_host.hpp"
#include "reboot/sessions/session_driver.hpp"

namespace reboot::front {
class LegacyFixedListeners;
class SessionFront;
}  // namespace reboot::front

namespace reboot::play {

class MatchTargets;

// What a driver releases at its end; it never stops the linked auto-server, a registry child.
struct PlaySessionReleases {
    MatchTargets& match_targets;
    front::SessionFront& front;
    front::LegacyFixedListeners& legacy_listeners;
};

// Strand-only. The registry's handle on one play session, which owns what start() took for it.
// Self-ends reach the registry as report_exit: our DLL's peer lost is Fatal, unresponsive is
// Unresponsive, a HostFatal is Fatal. A linked auto-server that ends first only raises
// play.linked_server_ended as a degraded condition.
class PlaySessionDriver final : public sessions::ISessionDriver {
public:
    PlaySessionDriver(SessionId session, PlaySessionReleases releases);
    // Withdraws the match target, removes the route and fixed listeners, then drops the peer
    // (revoking its token), the game session and the preflight pins and lease.
    ~PlaySessionDriver() override;
    PlaySessionDriver(const PlaySessionDriver&) = delete;
    PlaySessionDriver& operator=(const PlaySessionDriver&) = delete;

    void adopt(Preflight preflight);
    void adopt(std::unique_ptr<game_channel::ClientDllPeer> peer);
    void adopt(std::unique_ptr<ports::IGameSession> game);

    [[nodiscard]] PlaySessionState& state() noexcept { return state_; }

    // Shutdown to our DLL, then IGameSession::stop(request.grace); `done` once the game exited.
    void stop(const sessions::StopRequest& request, sessions::StopDone done) override;
    // Completes a pending stop.
    void on_game_exited();

private:
    PlaySessionReleases releases_;
    PlaySessionState state_;
    std::optional<Preflight> preflight_;
    std::unique_ptr<game_channel::ClientDllPeer> peer_;
    std::unique_ptr<ports::IGameSession> game_;
    std::optional<sessions::StopDone> pending_stop_;
};

}  // namespace reboot::play
