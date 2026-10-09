#include "secret_routing.hpp"

namespace rb::os_macos::platform {

SecretBackend write_backend(KeychainState state, bool aqua_session) noexcept {
    switch (state) {
        case KeychainState::Missing:
            return SecretBackend::File;
        case KeychainState::Locked:
            return aqua_session ? SecretBackend::Locked : SecretBackend::File;
        case KeychainState::Unlocked:
            return SecretBackend::Keychain;
    }
    return SecretBackend::File;
}

ports::SecretStoreKind store_kind(KeychainState state, bool aqua_session) noexcept {
    return write_backend(state, aqua_session) == SecretBackend::File ? ports::SecretStoreKind::File
                                                                     : ports::SecretStoreKind::Os;
}

SecretBackend read_backend_after_file_miss(KeychainState state) noexcept {
    switch (state) {
        case KeychainState::Missing:
            return SecretBackend::File;
        case KeychainState::Locked:
            return SecretBackend::Locked;
        case KeychainState::Unlocked:
            return SecretBackend::Keychain;
    }
    return SecretBackend::File;
}

}  // namespace rb::os_macos::platform
