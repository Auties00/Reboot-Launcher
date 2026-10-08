#pragma once

#include <optional>

#include "reboot/foundation/user_request.hpp"
#include "reboot/secrets/secret_target.hpp"

namespace reboot::play {

// Covers game-launch.orchestration.
// A question start() is expected to raise, so a UI or the CLI can prepare its answer, or put the
// secret, before it starts.
struct RequiredDecision {
    // ConfirmUntested, AutoServerConsent, NeedsSecret, ConfirmJoin, NeedsJoinPassword,
    // ConfirmUnencryptedUpstream or RosettaInstall.
    UserRequestKind kind{};
    // NeedsSecret: the secret whose put satisfies it ahead of start.
    std::optional<secrets::SecretTarget> secret;

    bool operator==(const RequiredDecision&) const = default;
};

}  // namespace reboot::play
