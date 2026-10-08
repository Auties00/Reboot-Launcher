#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/publish/publish_state.hpp"

namespace reboot::publish {

// EventKind::PublishStateChanged, coalesced per session.
struct PublishStateChanged {
    SessionId session;
    PublishState state;
};

}  // namespace reboot::publish
