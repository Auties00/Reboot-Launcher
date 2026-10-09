#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

#include "reboot/gameserver/describe_cache_document.hpp"
#include "reboot/storage/document_store.hpp"

using namespace reboot;
using namespace reboot::gameserver;
namespace gs = reboot::contracts::game_server;
namespace json = boost::json;

static_assert(storage::Document<DescribeCacheDocument>);

namespace {

Sha256Digest digest(u8 fill) {
    Sha256Digest out{};
    out.fill(fill);
    return out;
}

std::chrono::system_clock::time_point at(int seconds) {
    return std::chrono::system_clock::time_point{} + std::chrono::seconds{seconds};
}

GameServerDescription rich_description() {
    GameServerDescription description;
    description.protocol = gs::kGameServerProtocol;
    description.build = "1.2.3";
    description.supports = {gs::VersionSupport{"4.1", "10.40", {gs::ClRange{100, 200}, gs::ClRange{300, 300}}},
                            gs::VersionSupport{"14.40", "14.40", {}}};
    description.sockets = {gs::SocketSpec{gs::SocketRole::Game}, gs::SocketSpec{gs::SocketRole::Beacon}};
    description.capabilities = gs::ServerCapabilities{true, true, {"start_match", "drain"}};
    return description;
}

}  // namespace

TEST_CASE("the describe cache round-trips and keeps unknown members", "[gameserver][cache]") {
    DescribeCacheDocument document;
    document.put(CachedDescription{digest(0xab), at(10), rich_description()});
    document.put(CachedDescription{digest(0x01), at(20), GameServerDescription{}});
    document.unknown.emplace("future", 7);

    std::vector<storage::ValueIssue> issues;
    const DescribeCacheDocument read_back = DescribeCacheDocument::read(document.write(), issues);
    CHECK(issues.empty());
    REQUIRE(read_back.entries.size() == 2);
    const CachedDescription* entry = read_back.find(digest(0xab));
    REQUIRE(entry != nullptr);
    CHECK(entry->described_at == at(10));
    CHECK(same_description(entry->description, rich_description()));
    CHECK(read_back.unknown == document.unknown);
    std::string hex;
    for (int i = 0; i < 32; ++i) hex += "ab";
    CHECK(document.write().at("entries").as_array()[0].as_object().at("sha256").as_string() == hex);
}

TEST_CASE("put replaces the same binary and evicts the oldest past the limit", "[gameserver][cache]") {
    DescribeCacheDocument document;
    for (u8 i = 0; i < DescribeCacheDocument::kMaxEntries; ++i)
        document.put(CachedDescription{digest(i), at(100 - i), GameServerDescription{}});
    REQUIRE(document.entries.size() == DescribeCacheDocument::kMaxEntries);

    GameServerDescription replaced;
    replaced.build = "new";
    document.put(CachedDescription{digest(0), at(500), replaced});
    CHECK(document.entries.size() == DescribeCacheDocument::kMaxEntries);
    CHECK(document.find(digest(0))->description.build == "new");

    // digest(3) was described longest ago.
    document.put(CachedDescription{digest(9), at(600), GameServerDescription{}});
    CHECK(document.entries.size() == DescribeCacheDocument::kMaxEntries);
    CHECK(document.find(digest(3)) == nullptr);
    CHECK(document.find(digest(9)) != nullptr);

    document.erase(digest(9));
    CHECK(document.find(digest(9)) == nullptr);
    document.erase(digest(42));
    CHECK(document.entries.size() == DescribeCacheDocument::kMaxEntries - 1);
}

TEST_CASE("bad cache entries are dropped with a ValueIssue each", "[gameserver][cache]") {
    DescribeCacheDocument good;
    good.put(CachedDescription{digest(0x11), at(5), rich_description()});
    json::object values = good.write();
    json::array& entries = values.at("entries").as_array();
    const json::object valid = entries[0].as_object();

    json::object short_digest = valid;
    short_digest["sha256"] = "abcd";
    json::object upper_digest = valid;
    upper_digest["sha256"] = std::string(64, 'A');
    json::object no_time = valid;
    no_time.erase("described_at");
    json::object bad_role = valid;
    bad_role["description"].as_object()["sockets"] = json::array{"game", "lobby"};
    json::object big_protocol = valid;
    big_protocol["description"].as_object()["protocol"] = 1ull << 40;
    json::object no_capabilities = valid;
    no_capabilities["description"].as_object().erase("capabilities");
    entries.emplace_back(42);
    entries.emplace_back(short_digest);
    entries.emplace_back(upper_digest);
    entries.emplace_back(no_time);
    entries.emplace_back(bad_role);
    entries.emplace_back(big_protocol);
    entries.emplace_back(no_capabilities);

    std::vector<storage::ValueIssue> issues;
    const DescribeCacheDocument document = DescribeCacheDocument::read(values, issues);
    REQUIRE(document.entries.size() == 1);
    CHECK(document.find(digest(0x11)) != nullptr);
    REQUIRE(issues.size() == 7);
    CHECK(issues[0].path == "entries[1]");
    CHECK(issues[0].reason.id == "gameserver.cache_entry_invalid");
    CHECK(issues[3].reason.find_arg("member") != nullptr);
    CHECK(issues[6].path == "entries[7]");
}

TEST_CASE("a non-list entries member is one issue and an empty cache", "[gameserver][cache]") {
    std::vector<storage::ValueIssue> issues;
    const DescribeCacheDocument document = DescribeCacheDocument::read(json::parse(R"({"entries": 3})").as_object(), issues);
    CHECK(document.entries.empty());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "entries");

    issues.clear();
    CHECK(DescribeCacheDocument::read(json::object{}, issues).entries.empty());
    CHECK(issues.empty());
}

TEST_CASE("reading more entries than the limit keeps the newest", "[gameserver][cache]") {
    json::array entries;
    for (u8 i = 0; i < 6; ++i) {
        DescribeCacheDocument one;
        one.put(CachedDescription{digest(i), at(i), GameServerDescription{}});
        entries.push_back(one.write().at("entries").as_array()[0]);
    }
    json::object values;
    values.emplace("entries", entries);
    std::vector<storage::ValueIssue> issues;
    const DescribeCacheDocument document = DescribeCacheDocument::read(values, issues);
    CHECK(issues.empty());
    CHECK(document.entries.size() == DescribeCacheDocument::kMaxEntries);
    CHECK(document.find(digest(0)) == nullptr);
    CHECK(document.find(digest(1)) == nullptr);
    CHECK(document.find(digest(5)) != nullptr);
}
