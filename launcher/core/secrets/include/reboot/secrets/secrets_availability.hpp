#pragma once

#include <optional>

#include "reboot/foundation/types.hpp"
#include "reboot/ports/secret_store.hpp"

namespace rb::secrets {

enum class SecretsUnavailableReason : u8 {
    // The store reports kind Unavailable: macOS outside Aqua, where there is no file fallback.
    NoOsStore,
    LoadFailed,
    LoadTimedOut,
};

// Reported to clients as HelloAck.secrets_available. When unavailable, every secret is held
// for this engine run only.
struct SecretsAvailability {
    // File when the platform store fell back to owner-only files in this logon (Windows without
    // Credential Manager, Linux without a Secret Service); Remember then writes there.
    ports::SecretStoreKind store = ports::SecretStoreKind::Unavailable;
    std::optional<SecretsUnavailableReason> unavailable;

    [[nodiscard]] bool available() const noexcept { return !unavailable.has_value(); }

    bool operator==(const SecretsAvailability&) const = default;
};

}  // namespace rb::secrets
