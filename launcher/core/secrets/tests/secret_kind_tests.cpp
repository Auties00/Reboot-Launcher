#include <catch2/catch_test_macros.hpp>

#include "reboot/secrets/secret_kind.hpp"

using namespace reboot::secrets;

TEST_CASE("a remote password is stored only on opt-in", "[secrets]") {
    STATIC_REQUIRE(default_retention(SecretKind::RemoteBackendPassword) == Retention::Session);
    STATIC_REQUIRE(retention_allowed(SecretKind::RemoteBackendPassword, Retention::Session));
    STATIC_REQUIRE(retention_allowed(SecretKind::RemoteBackendPassword, Retention::Remember));
}

TEST_CASE("host and join passwords have a fixed retention", "[secrets]") {
    STATIC_REQUIRE(default_retention(SecretKind::HostJoinPassword) == Retention::Remember);
    STATIC_REQUIRE_FALSE(retention_allowed(SecretKind::HostJoinPassword, Retention::Session));
    STATIC_REQUIRE(default_retention(SecretKind::JoinPassword) == Retention::Session);
    STATIC_REQUIRE_FALSE(retention_allowed(SecretKind::JoinPassword, Retention::Remember));
}

TEST_CASE("only the host join password can be revealed", "[secrets]") {
    STATIC_REQUIRE(revealable(SecretKind::HostJoinPassword));
    STATIC_REQUIRE_FALSE(revealable(SecretKind::RemoteBackendPassword));
    STATIC_REQUIRE_FALSE(revealable(SecretKind::JoinPassword));
}

TEST_CASE("kind names are distinct and stable", "[secrets]") {
    STATIC_REQUIRE(kind_name(SecretKind::RemoteBackendPassword) == "remote-backend-password");
    STATIC_REQUIRE(kind_name(SecretKind::HostJoinPassword) == "host-join-password");
    STATIC_REQUIRE(kind_name(SecretKind::JoinPassword) == "join-password");
}
