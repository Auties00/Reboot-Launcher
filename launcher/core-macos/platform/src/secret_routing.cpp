#include "secret_routing.hpp"

namespace rb::os_macos::platform {

SecretBackend secret_backend(KeychainState state, bool aqua_session) noexcept {
    if (!aqua_session) return SecretBackend::Unavailable;
    switch (state) {
        case KeychainState::Missing: return SecretBackend::File;
        case KeychainState::Locked: return SecretBackend::Locked;
        case KeychainState::Unlocked: return SecretBackend::Keychain;
    }
    return SecretBackend::File;
}

ports::SecretStoreKind store_kind(KeychainState state, bool aqua_session) noexcept {
    switch (secret_backend(state, aqua_session)) {
        case SecretBackend::File: return ports::SecretStoreKind::File;
        case SecretBackend::Keychain:
        case SecretBackend::Locked: return ports::SecretStoreKind::Os;
        case SecretBackend::Unavailable: break;
    }
    return ports::SecretStoreKind::Unavailable;
}

}  // namespace rb::os_macos::platform
