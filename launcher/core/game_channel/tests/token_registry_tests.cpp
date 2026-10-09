#include <array>
#include <chrono>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "game_channel_test_kit.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/game_channel/token_registry.hpp"
#include "reboot/testing/fake_random.hpp"

namespace reboot::game_channel {
namespace {

using namespace std::chrono_literals;
using test::gc::PeerRole;

struct Registry {
    testing::FakeRandom random{3};
    Redactor redactor;
    TokenRegistry tokens{random, redactor};
};

[[nodiscard]] PeerKey client_key(SessionId session, std::string module = "rb_client.dll") {
    return PeerKey{session, PeerRole::ClientDll, std::move(module)};
}

[[nodiscard]] SessionId session_of(u8 last) {
    Uuid uuid{};
    uuid.bytes[15] = last;
    return SessionId{uuid};
}

TEST_CASE("a token's environment value is 43 characters of unpadded base64url", "[game_channel][token]") {
    Registry r;
    const auto token = r.tokens.issue(client_key(session_of(1)), RunnerMultiplier::Native);
    REQUIRE(token);
    const std::string value = token->env_value().reveal();
    CHECK(value.size() == 43);
    for (const char c : value) {
        const bool alphabet = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        CHECK(alphabet);
    }
    const std::array<u8, 32> bytes = test::token_bytes(*token);
    CHECK(token->matches(bytes));
    std::array<u8, 32> other = bytes;
    other[31] ^= 1;
    CHECK_FALSE(token->matches(other));
}

TEST_CASE("the environment value is redacted while the token lives", "[game_channel][token]") {
    Registry r;
    const PeerKey key = client_key(session_of(1));
    const auto token = r.tokens.issue(key, RunnerMultiplier::Native);
    REQUIRE(token);
    const std::string value = token->env_value().reveal();
    CHECK(r.redactor.apply("REBOOT_CTL_TOKEN=" + value).find(value) == std::string::npos);
    r.tokens.revoke(key);
    CHECK(r.redactor.apply("REBOOT_CTL_TOKEN=" + value).find(value) != std::string::npos);
}

TEST_CASE("a key holds one live token", "[game_channel][token]") {
    Registry r;
    const PeerKey key = client_key(session_of(1));
    REQUIRE(r.tokens.issue(key, RunnerMultiplier::Native));
    const auto again = r.tokens.issue(key, RunnerMultiplier::Native);
    REQUIRE_FALSE(again);
    CHECK(again.error().id == "game_channel.duplicate_peer");
    CHECK(r.tokens.issue(client_key(session_of(1), "other.dll"), RunnerMultiplier::Native));
    CHECK(r.tokens.issue(client_key(session_of(2)), RunnerMultiplier::Native));
    r.tokens.revoke(key);
    CHECK(r.tokens.issue(key, RunnerMultiplier::Native));
}

TEST_CASE("claim routes a token to its key once", "[game_channel][token]") {
    Registry r;
    const PeerKey first = client_key(session_of(1));
    const PeerKey second{session_of(2), PeerRole::Winhost, "reboot-winhost.exe"};
    const auto first_token = r.tokens.issue(first, RunnerMultiplier::Native);
    const auto second_token = r.tokens.issue(second, RunnerMultiplier::Wine);
    REQUIRE(first_token);
    REQUIRE(second_token);

    const auto claimed = r.tokens.claim(test::token_bytes(*second_token), PeerRole::Winhost);
    REQUIRE(claimed);
    CHECK(*claimed == second);

    const auto duplicate = r.tokens.claim(test::token_bytes(*second_token), PeerRole::Winhost);
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.error().id == "game_channel.duplicate_peer");

    const auto wrong_role = r.tokens.claim(test::token_bytes(*first_token), PeerRole::Winhost);
    REQUIRE_FALSE(wrong_role);
    CHECK(wrong_role.error().id == "game_channel.role_mismatch");
    // A role mismatch does not use the token up.
    CHECK(r.tokens.claim(test::token_bytes(*first_token), PeerRole::ClientDll));

    std::array<u8, 32> unknown{};
    unknown.fill(0xAB);
    const auto stranger = r.tokens.claim(unknown, PeerRole::ClientDll);
    REQUIRE_FALSE(stranger);
    CHECK(stranger.error().id == "game_channel.unknown_token");
}

TEST_CASE("a revoked token is unknown", "[game_channel][token]") {
    Registry r;
    const PeerKey key = client_key(session_of(1));
    const auto token = r.tokens.issue(key, RunnerMultiplier::Native);
    REQUIRE(token);
    const std::array<u8, 32> bytes = test::token_bytes(*token);
    r.tokens.revoke(key);
    const auto claimed = r.tokens.claim(bytes, PeerRole::ClientDll);
    REQUIRE_FALSE(claimed);
    CHECK(claimed.error().id == "game_channel.unknown_token");
}

TEST_CASE("the Hello deadline follows the slowest unclaimed token", "[game_channel][token]") {
    Registry r;
    CHECK_FALSE(r.tokens.hello_deadline());
    const auto native = r.tokens.issue(client_key(session_of(1)), RunnerMultiplier::Native);
    REQUIRE(native);
    CHECK(r.tokens.hello_deadline() == 2000ms);
    const auto rosetta = r.tokens.issue(client_key(session_of(2)), RunnerMultiplier::RosettaFirstRun);
    REQUIRE(rosetta);
    CHECK(r.tokens.hello_deadline() == 8000ms);
    REQUIRE(r.tokens.claim(test::token_bytes(*rosetta), PeerRole::ClientDll));
    CHECK(r.tokens.hello_deadline() == 2000ms);
    REQUIRE(r.tokens.claim(test::token_bytes(*native), PeerRole::ClientDll));
    CHECK_FALSE(r.tokens.hello_deadline());
}

}  // namespace
}  // namespace reboot::game_channel
