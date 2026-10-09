#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/publish/publish_state.hpp"

namespace rb::publish {

// EventKind::PublishStateChanged, coalesced per session.
struct PublishStateChanged {
    SessionId session;
    PublishState state;
};

}  // namespace rb::publish
