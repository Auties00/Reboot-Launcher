#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/ports/secret_store.hpp"

namespace rb::os_macos::platform {

enum class KeychainState : u8 { Missing, Locked, Unlocked };

// Where KeychainSecretStore keeps a secret. Outside Aqua there is no store at all, since macOS has
// no file fallback there; in Aqua the files stand in only when there is no login keychain.
enum class SecretBackend : u8 { File, Keychain, Locked, Unavailable };

// The backend of every put, get and erase. Reads still try the files first, since a file is only
// ever newer than the keychain item.
[[nodiscard]] SecretBackend secret_backend(KeychainState state, bool aqua_session) noexcept;
// Unavailable outside Aqua, which the engine reports as secrets unavailable (NoOsStore).
[[nodiscard]] ports::SecretStoreKind store_kind(KeychainState state, bool aqua_session) noexcept;

}  // namespace rb::os_macos::platform
