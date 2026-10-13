#include <catch2/catch_test_macros.hpp>

#include "secret_routing.hpp"

using namespace rb::os_macos::platform;
using rb::ports::SecretStoreKind;

TEST_CASE("outside Aqua there is no secret store, not even files", "[secret_routing]") {
    for (const KeychainState state : {KeychainState::Missing, KeychainState::Locked, KeychainState::Unlocked}) {
        CHECK(secret_backend(state, false) == SecretBackend::Unavailable);
        CHECK(store_kind(state, false) == SecretStoreKind::Unavailable);
    }
}

TEST_CASE("in Aqua without a login keychain secrets live in files", "[secret_routing]") {
    CHECK(secret_backend(KeychainState::Missing, true) == SecretBackend::File);
    CHECK(store_kind(KeychainState::Missing, true) == SecretStoreKind::File);
}

TEST_CASE("in Aqua an unlocked keychain takes every secret", "[secret_routing]") {
    CHECK(secret_backend(KeychainState::Unlocked, true) == SecretBackend::Keychain);
    CHECK(store_kind(KeychainState::Unlocked, true) == SecretStoreKind::Os);
}

TEST_CASE("in Aqua a locked keychain fails rather than writing a file", "[secret_routing]") {
    CHECK(secret_backend(KeychainState::Locked, true) == SecretBackend::Locked);
    CHECK(store_kind(KeychainState::Locked, true) == SecretStoreKind::Os);
}
