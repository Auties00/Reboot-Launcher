#include <array>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/front/upstream_policy.hpp"

using namespace reboot;
using namespace reboot::front;

namespace {

std::vector<u8> golden(std::string_view name) {
    std::ifstream stream(std::string(REBOOT_FRONT_TEST_DATA) + "/" + std::string(name), std::ios::binary);
    REQUIRE(stream);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

UpstreamOrigin origin(std::string_view url) { return *parse_upstream_origin(url); }

Endpoint at(std::string_view address, u16 port) { return {*IpAddress::parse(address), Port{port}}; }

constexpr std::string_view kSystemFile = "/fortnite/api/cloudstorage/system/DefaultEngine.ini";
constexpr std::string_view kTicketPath = "/fortnite/api/game/v2/matchmakingservice/ticket/player/abc";
constexpr std::array<Port, 2> kFrontPorts{Port{49152}, Port{3551}};

}  // namespace

TEST_CASE("XMPP origins are learned from a cloudstorage system file", "[front][policy]") {
    UpstreamPolicy policy(origin("http://127.0.0.1:3552"));
    const auto added = policy.learn(kSystemFile, golden("lawin_default_engine.ini"));
    REQUIRE(added.size() == 1);
    CHECK(added[0] == origin("ws://127.0.0.1:80"));
    CHECK(policy.allows(origin("ws://127.0.0.1:80")));
    CHECK(!policy.allows(origin("ws://ignored.example:1")));
}

TEST_CASE("a portless ServerAddr learns the scheme's port and ServerPort", "[front][policy]") {
    UpstreamPolicy policy(origin("https://backend.example.com"));
    const auto added = policy.learn("/fortnite/api/cloudstorage/system/a1b2c3", golden("portless_default_engine.ini"));
    REQUIRE(added.size() == 2);
    CHECK(added[0] == origin("wss://xmpp.example.com:443"));
    CHECK(added[1] == origin("wss://xmpp.example.com:5222"));
}

TEST_CASE("a matchmaking ticket teaches its serviceUrl once", "[front][policy]") {
    UpstreamPolicy policy(origin("https://backend.example.com"));
    const auto added = policy.learn(std::string(kTicketPath) + "?bucketId=x", golden("matchmaking_ticket.json"));
    REQUIRE(added.size() == 1);
    CHECK(added[0] == origin("wss://mm.example.com"));
    CHECK(policy.learn(kTicketPath, golden("matchmaking_ticket.json")).empty());
}

TEST_CASE("other paths and oversized bodies teach nothing", "[front][policy]") {
    UpstreamPolicy policy(origin("https://backend.example.com"));
    CHECK(policy.learn("/fortnite/api/cloudstorage/user/x", golden("lawin_default_engine.ini")).empty());
    CHECK(policy.learn("/fortnite/api/cloudstorage/system/", golden("lawin_default_engine.ini")).empty());
    const std::vector<u8> huge(kLearnBodyCap + 1, u8{'['});
    CHECK(policy.learn(kSystemFile, huge).empty());
    CHECK(policy.learned().empty());
}

TEST_CASE("relays match scheme, host and port exactly", "[front][policy]") {
    const UpstreamPolicy policy(origin("https://backend.example.com"));
    CHECK(policy.allows(origin("wss://backend.example.com")));
    CHECK(!policy.allows(origin("wss://backend.example.com:8443")));
    CHECK(!policy.allows(origin("ws://backend.example.com:443")));
}

TEST_CASE("a relay never loops into the front's own listeners", "[front][policy][loop]") {
    UpstreamPolicy policy(origin("http://127.0.0.1:3551"));
    policy.learn(kSystemFile, golden("lawin_default_engine.ini"));
    CHECK(!policy.allows_connect(origin("http://127.0.0.1:3551"), at("127.0.0.1", 3551), kFrontPorts));
    CHECK(policy.allows_connect(origin("http://127.0.0.1:3551"), at("127.0.0.1", 3551), {}));
    CHECK(!policy.allows_connect(origin("ws://127.0.0.1:49152"), at("127.0.0.1", 49152), kFrontPorts));
    CHECK(policy.allows_connect(origin("ws://127.0.0.1:80"), at("127.0.0.1", 80), kFrontPorts));
}

TEST_CASE("only a loopback backend reaches this machine", "[front][policy][loop]") {
    UpstreamPolicy remote(origin("https://backend.example.com"));
    remote.learn(kSystemFile, golden("lawin_default_engine.ini"));
    CHECK(remote.allows_connect(origin("https://backend.example.com"), at("203.0.113.5", 443), kFrontPorts));
    CHECK(!remote.allows_connect(origin("https://backend.example.com"), at("127.0.0.1", 443), kFrontPorts));
    CHECK(!remote.allows_connect(origin("ws://127.0.0.1:80"), at("127.0.0.1", 80), kFrontPorts));
    CHECK(!remote.allows_connect(origin("ws://127.0.0.1:80"), at("0.0.0.0", 80), kFrontPorts));
    CHECK(!remote.allows_connect(origin("wss://elsewhere.example.com"), at("203.0.113.6", 443), kFrontPorts));
}
