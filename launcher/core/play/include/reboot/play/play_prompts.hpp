#pragma once

#include "reboot/foundation/version.hpp"
#include "reboot/support/support_query.hpp"
#include "reboot/support/support_verdict.hpp"

namespace rb::play {

// Payload of UserRequestKind::AutoServerConsent; the answer is a bool, true to start the server.
struct AutoServerConsentPrompt {
    GameVersion version;
};

// Payload of UserRequestKind::ConfirmUntested raised by play. Shaped and answered like
// host::UntestedHostPrompt: a bool, true to play anyway; nothing is remembered.
struct UntestedPlayPrompt {
    support::SupportQuery query;
    // With the linked auto-server, the verdict that set AutoServerVerdict::tier.
    support::SupportVerdict verdict;
};

}  // namespace rb::play
