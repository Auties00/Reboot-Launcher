#pragma once

#include "reboot/secrets/secret_state.hpp"
#include "reboot/secrets/secret_target.hpp"

namespace rb::secrets {

// EventKind::SecretStateChanged, coalesced per target; it never carries a value.
struct SecretStateChangedEvent {
    SecretTarget target;
    SecretState state;
};

}  // namespace rb::secrets
