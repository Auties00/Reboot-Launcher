#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/ports/secret_store.hpp"

namespace rb::os_macos::platform {

enum class KeychainState : u8 { Missing, Locked, Unlocked };

// Where KeychainSecretStore keeps a secret: the files stand in when there is no login keychain,
// or when it is locked outside Aqua, where nobody can unlock it.
enum class SecretBackend : u8 { File, Keychain, Locked };

[[nodiscard]] SecretBackend write_backend(KeychainState state, bool aqua_session) noexcept;
[[nodiscard]] ports::SecretStoreKind store_kind(KeychainState state, bool aqua_session) noexcept;
// Reads try the files first, since a file is only ever newer than the keychain item; on a miss
// a locked keychain may still hold the key.
[[nodiscard]] SecretBackend read_backend_after_file_miss(KeychainState state) noexcept;

}  // namespace rb::os_macos::platform
