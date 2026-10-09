#include <catch2/catch_test_macros.hpp>
#include <initializer_list>
#include <string_view>
#include <utility>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/secrets/secret_error.hpp"
#include "reboot/secrets/secret_target.hpp"

using namespace rb;
using namespace rb::secrets;

namespace {

std::string_view scope_text(const Result<SecretTarget>& target) { return target->scope.text(); }

}  // namespace

TEST_CASE("a backend scope is lowercased and keeps only a given port", "[secrets]") {
    const auto plain = SecretTarget::parse(SecretKind::RemoteBackendPassword, "Backend.Example.COM");
    REQUIRE(plain.has_value());
    CHECK(scope_text(plain) == "backend.example.com");

    const auto with_port = SecretTarget::parse(SecretKind::RemoteBackendPassword, "Backend.Example.com:3551");
    REQUIRE(with_port.has_value());
    CHECK(scope_text(with_port) == "backend.example.com:3551");
    CHECK(*with_port != *plain);
}

TEST_CASE("an IPv6 backend with a port stays bracketed", "[secrets]") {
    const auto target = SecretTarget::parse(SecretKind::RemoteBackendPassword, "[FE80::1]:443");
    REQUIRE(target.has_value());
    CHECK(scope_text(target) == "[fe80::1]:443");
    const auto reparsed = SecretTarget::parse(SecretKind::RemoteBackendPassword, scope_text(target));
    REQUIRE(reparsed.has_value());
    CHECK(*reparsed == *target);
}

TEST_CASE("a parsed scope equals the one built from typed values", "[secrets]") {
    const auto target = SecretTarget::parse(SecretKind::RemoteBackendPassword, "HOST:80");
    REQUIRE(target.has_value());
    const SecretTarget built{SecretKind::RemoteBackendPassword, SecretScope::backend(HostPort{"host", Port{80}})};
    CHECK(*target == built);
}

TEST_CASE("a host profile scope is the lowercase uuid", "[secrets]") {
    const auto target = SecretTarget::parse(SecretKind::HostJoinPassword, "0A1B2C3D-4E5F-6071-8293-A4B5C6D7E8F9");
    REQUIRE(target.has_value());
    CHECK(scope_text(target) == "0a1b2c3d-4e5f-6071-8293-a4b5c6d7e8f9");
}

TEST_CASE("a join request scope is its decimal id", "[secrets]") {
    const auto target = SecretTarget::parse(SecretKind::JoinPassword, "0042");
    REQUIRE(target.has_value());
    CHECK(scope_text(target) == "42");
    CHECK(target->scope == SecretScope::join_request(RequestId{42}));
}

TEST_CASE("scope text that does not fit the kind is rejected", "[secrets]") {
    const auto cases = {
        std::pair{SecretKind::RemoteBackendPassword, std::string_view{}},
        std::pair{SecretKind::RemoteBackendPassword, std::string_view{"host:notaport"}},
        std::pair{SecretKind::HostJoinPassword, std::string_view{"backend.example.com"}},
        std::pair{SecretKind::JoinPassword, std::string_view{"12a"}},
        std::pair{SecretKind::JoinPassword, std::string_view{"-1"}},
        std::pair{SecretKind::JoinPassword, std::string_view{" 1"}},
    };
    for (const auto& [kind, scope] : cases) {
        const auto target = SecretTarget::parse(kind, scope);
        REQUIRE_FALSE(target.has_value());
        CHECK(has_error(target.error(), SecretError::InvalidScope));
        CHECK(exit_code_for(target.error()) == 2);
    }
}
