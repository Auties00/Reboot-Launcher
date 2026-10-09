#include <catch2/catch_test_macros.hpp>

#include "secret_routing.hpp"

using namespace rb::os_macos::platform;
using rb::ports::SecretStoreKind;

TEST_CASE("without a login keychain secrets live in files", "[secret_routing]") {
    CHECK(write_backend(KeychainState::Missing, true) == SecretBackend::File);
    CHECK(write_backend(KeychainState::Missing, false) == SecretBackend::File);
    CHECK(store_kind(KeychainState::Missing, true) == SecretStoreKind::File);
    CHECK(read_backend_after_file_miss(KeychainState::Missing) == SecretBackend::File);
}

TEST_CASE("an unlocked keychain takes every write", "[secret_routing]") {
    CHECK(write_backend(KeychainState::Unlocked, true) == SecretBackend::Keychain);
    CHECK(write_backend(KeychainState::Unlocked, false) == SecretBackend::Keychain);
    CHECK(store_kind(KeychainState::Unlocked, false) == SecretStoreKind::Os);
    CHECK(read_backend_after_file_miss(KeychainState::Unlocked) == SecretBackend::Keychain);
}

TEST_CASE("a locked keychain fails in Aqua and falls back to files outside it", "[secret_routing]") {
    CHECK(write_backend(KeychainState::Locked, true) == SecretBackend::Locked);
    CHECK(store_kind(KeychainState::Locked, true) == SecretStoreKind::Os);
    CHECK(write_backend(KeychainState::Locked, false) == SecretBackend::File);
    CHECK(store_kind(KeychainState::Locked, false) == SecretStoreKind::File);
}

TEST_CASE("a key missing from the files while the keychain is locked cannot be answered", "[secret_routing]") {
    CHECK(read_backend_after_file_miss(KeychainState::Locked) == SecretBackend::Locked);
}
