#pragma once

#include <memory>
#include <optional>

#include "reboot/backend/login_observed_event.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/play/play_plan.hpp"
#include "reboot/play/play_request.hpp"
#include "reboot/play/play_service.hpp"
#include "reboot/play/play_session_state.hpp"

namespace rb {
class EventBus;
class Executor;
class IRandom;
class Redactor;
class UserRequestRegistry;
}  // namespace rb

namespace rb::ports {
class ISystemInfo;
}

namespace rb::sessions {
class SessionRegistry;
}

namespace rb::game_channel {
class GameChannelListener;
}

namespace rb::play {

class MatchTargets;
class PlayEnv;

struct PlayCoreDeps {
    PlayEnv& env;
    sessions::SessionRegistry& sessions;
    game_channel::GameChannelListener& channel;
    MatchTargets& match_targets;
    ports::ISystemInfo& system;
    IRandom& random;
    Redactor& redactor;
    UserRequestRegistry& requests;
    OpRegistry& ops;
    EventBus& events;
    Executor& strand;
};

// PlayService's state machine over PlayEnv; see PlayService for the rules it keeps.
class PlayCore {
public:
    PlayCore(PlayCoreDeps deps, PlayServiceOptions options);
    ~PlayCore();
    PlayCore(const PlayCore&) = delete;
    PlayCore& operator=(const PlayCore&) = delete;

    [[nodiscard]] Result<PlayPlan> plan(const PlayRequest& request) const;
    Result<OpHandle> start(PlayRequest request, DisconnectPolicy policy = DisconnectPolicy::Detached);
    void on_login_observed(const backend::LoginObservedEvent& event);
    [[nodiscard]] std::optional<PlaySessionState> state(SessionId session) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::play
