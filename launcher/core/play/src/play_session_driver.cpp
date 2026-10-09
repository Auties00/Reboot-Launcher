#include "play_session_driver.hpp"

#include <utility>

#include "play_env.hpp"
#include "reboot/play/match_targets.hpp"

namespace rb::play {

PlaySessionDriver::PlaySessionDriver(PlaySessionReleases releases, PlaySessionHooks hooks)
    : releases_(releases), hooks_(std::move(hooks)) {}

PlaySessionDriver::~PlaySessionDriver() {
    release();
    if (UniqueFunction<void()> destroyed = std::move(hooks_.on_destroyed)) destroyed();
}

void PlaySessionDriver::bind(SessionId session) {
    session_ = session;
    state_.session = session;
}

void PlaySessionDriver::adopt(Preflight preflight) { preflight_.emplace(std::move(preflight)); }

void PlaySessionDriver::adopt(std::unique_ptr<game_channel::ClientDllPeer> peer) { peer_ = std::move(peer); }

void PlaySessionDriver::adopt(std::unique_ptr<ports::IGameSession> game) { game_ = std::move(game); }

void PlaySessionDriver::stop(const sessions::StopRequest& request, sessions::StopDone done) {
    state_.ending = true;
    if (hooks_.on_stop) hooks_.on_stop(request);
    if (!game_ || game_exited_ || released_) {
        done(Result<void>{});
        return;
    }
    pending_stop_ = std::move(done);
    // Our DLL ends the game cleanly; the session host kills what is left after the grace.
    if (peer_ && peer_->welcomed()) static_cast<void>(peer_->shutdown([](Result<void>) {}));
    game_->stop(request.grace);
}

void PlaySessionDriver::on_game_exited() {
    game_exited_ = true;
    if (!pending_stop_) return;
    sessions::StopDone done = std::move(*pending_stop_);
    pending_stop_.reset();
    done(Result<void>{});
}

void PlaySessionDriver::detach() {
    hooks_ = {};
    release();
    if (pending_stop_) on_game_exited();
}

void PlaySessionDriver::release() {
    if (released_) return;
    released_ = true;
    if (session_) {
        releases_.match_targets.withdraw(*session_);
        releases_.env.remove_route(*session_);
        releases_.env.close_fixed(*session_);
    }
    peer_.reset();
    game_.reset();
    preflight_.reset();
}

}  // namespace rb::play
