#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "host_test_support.hpp"
#include "reboot/host/host_profiles_document.hpp"

using namespace reboot;
using namespace reboot::host;
using namespace std::chrono_literals;
namespace json = boost::json;

namespace {

HostProfile full_profile() {
    HostProfile profile = new_profile(test::profile_id(3), "Weekend", HostListing::Listed);
    profile.revision = 7;
    profile.version = HostVersion{*GameVersion::parse("14.40"), Changelist{14550713}};
    profile.port = PinnedPorts{Port{7800}};
    profile.port_mapping = false;
    profile.server_name = "Weekend cup";
    profile.description = "Solos only";
    profile.match.playlist = "Playlist_DefaultSolo";
    profile.match.start = gameserver::AutoAtPlayers{20};
    profile.match.max_players = 100;
    profile.match.tick_rate = 30;
    profile.match_end = MatchEndPolicy{MatchEndAction::Shutdown, 30s};
    profile.operators.operator_cidrs = {*IpCidr::parse("10.0.0.0/8")};
    HostBan ip_ban;
    ip_ban.address = *IpCidr::parse("203.0.113.7");
    ip_ban.reason = "griefing";
    ip_ban.created = std::chrono::system_clock::time_point{} + 1000h;
    ip_ban.expires = ip_ban.created + 24h;
    HostBan account_ban;
    account_ban.account_id = "Cheater-abc123";
    account_ban.created = ip_ban.created;
    profile.operators.bans = {ip_ban, account_ban};
    profile.update_policy = HostUpdatePolicy::Manual;
    return profile;
}

json::object values_of(std::string_view text) { return json::parse(text).as_object(); }

std::vector<std::string> issue_paths(const std::vector<storage::ValueIssue>& issues) {
    std::vector<std::string> out;
    for (const storage::ValueIssue& issue : issues) out.push_back(issue.path);
    return out;
}

}  // namespace

TEST_CASE("a host profiles document survives a write and a read", "[host][document]") {
    HostProfilesDocument document;
    document.profiles = {auto_profile(), full_profile()};
    document.unknown.emplace("future", "kept");

    const json::object written = document.write();
    std::vector<storage::ValueIssue> issues;
    const HostProfilesDocument read = HostProfilesDocument::read(written, issues);
    CHECK(issues.empty());
    REQUIRE(read.profiles.size() == 2);
    CHECK(read.write() == written);
    CHECK(read.unknown.at("future").as_string() == "kept");

    const HostProfile& profile = read.profiles[1];
    CHECK(profile.revision == 7);
    CHECK(std::get<PinnedPorts>(profile.port).first == Port{7800});
    CHECK(profile.version->cl == Changelist{14550713});
    CHECK(profile.match_end.action == MatchEndAction::Shutdown);
    REQUIRE(profile.operators.bans.size() == 2);
    CHECK(profile.operators.bans[1].evadable());
    CHECK(profile.operators.bans[0].expires.has_value());
    CHECK(profile.update_policy == HostUpdatePolicy::Manual);
}

TEST_CASE("an invalid user profile is dropped with an issue", "[host][document]") {
    const json::object values = values_of(R"({"profiles": [
        {"id": "11111111-1111-4111-8111-111111111111", "name": "ok"},
        {"id": "22222222-2222-4222-8222-222222222222", "name": "bad port", "port": {"pinned": "x"}},
        {"id": "33333333-3333-4333-8333-333333333333", "name": "low", "port": {"pinned": 80}},
        {"id": "44444444-4444-4444-8444-444444444444"},
        {"id": "not a uuid", "name": "no id"},
        {"id": "55555555-5555-4555-8555-555555555555", "name": "no target", "operators": {"bans": [{"reason": "?"}]}}
    ]})");
    std::vector<storage::ValueIssue> issues;
    const HostProfilesDocument document = HostProfilesDocument::read(values, issues);
    REQUIRE(document.profiles.size() == 1);
    CHECK(document.profiles[0].name == "ok");
    CHECK(issue_paths(issues) == std::vector<std::string>{"profiles[1]", "profiles[2]", "profiles[3]", "profiles[4]", "profiles[5]"});
    CHECK(issues[1].reason.id == "host.invalid_port_policy");
    CHECK(issues[4].reason.id == "host.ban_without_target");
}

TEST_CASE("a built-in profile keeps its id and falls back member by member", "[host][document]") {
    const json::object values = values_of(R"({"profiles": [
        {"id": "7c1e4a52-0d3b-4f11-9a02-5245424f4f54", "name": "auto", "listing": "listed", "port": {"pinned": 9000},
         "server_name": "mine", "match_end": {"action": "explode"}}
    ]})");
    std::vector<storage::ValueIssue> issues;
    const HostProfilesDocument document = HostProfilesDocument::read(values, issues);
    REQUIRE(document.profiles.size() == 1);
    const HostProfile& profile = document.profiles[0];
    CHECK(profile.is_auto());
    CHECK(profile.listing == HostListing::Unlisted);
    CHECK(std::get<PinnedPorts>(profile.port).first == Port{9000});
    CHECK(profile.server_name == "mine");
    CHECK(profile.match_end == MatchEndPolicy{});
    CHECK(issue_paths(issues) == std::vector<std::string>{"profiles[0].match_end", "profiles[0].listing"});
}

TEST_CASE("a repeated id or name keeps the first profile", "[host][document]") {
    const json::object values = values_of(R"({"profiles": [
        {"id": "11111111-1111-4111-8111-111111111111", "name": "Main"},
        {"id": "11111111-1111-4111-8111-111111111111", "name": "other"},
        {"id": "22222222-2222-4222-8222-222222222222", "name": "MAIN"}
    ]})");
    std::vector<storage::ValueIssue> issues;
    const HostProfilesDocument document = HostProfilesDocument::read(values, issues);
    REQUIRE(document.profiles.size() == 1);
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].reason.id == "host.duplicate_profile");
    CHECK(issues[1].reason.id == "host.profile_name_taken");
}

TEST_CASE("a document without profiles reads empty", "[host][document]") {
    std::vector<storage::ValueIssue> issues;
    CHECK(HostProfilesDocument::read({}, issues).profiles.empty());
    CHECK(HostProfilesDocument::read(values_of(R"({"profiles": 3})"), issues).profiles.empty());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "profiles");
    CHECK(HostProfilesDocument::upgrade(values_of(R"({"profiles": []})"), 1).has_value());
}
