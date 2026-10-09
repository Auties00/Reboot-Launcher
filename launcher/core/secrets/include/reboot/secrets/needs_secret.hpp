#pragma once

#include <optional>

#include "reboot/foundation/types.hpp"
#include "reboot/secrets/secret_state.hpp"
#include "reboot/secrets/secret_target.hpp"

namespace rb::secrets {

enum class NeedsSecretReason : u8 { Missing, Rejected };

// Payload of a UserRequestKind::NeedsSecret request. A UI prompts, or the CLI reads
// --password-stdin or REBOOT_PASSWORD; either puts the secret, then answers SecretProvided.
struct NeedsSecret {
    std::optional<SessionId> session;
    // For RemoteBackendPassword the scope is the backend host to show.
    SecretTarget target;
    NeedsSecretReason reason{};
    // OsStore or FileStore: where "remember" would save it, so a UI can say when that is a file.
    // Empty when the kind or the store rules remembering out.
    std::optional<SecretLocation> remember_location;
};

// The only accepted answer to NeedsSecret; it is refused until a fresh secret was put.
struct SecretProvided {};

}  // namespace rb::secrets
