#include <catch2/catch_test_macros.hpp>

#include <random>

#include "client/view_mirror.hpp"
#include "registry/search.hpp"
#include "registry/validation.hpp"

using namespace sb;
using namespace sb::registry;

namespace {

PubEntry make(u32 h, std::string name, std::string author, u32 players, bool pwd, wire::Region region, std::string version) {
    PubEntry p;
    p.handle = h;
    p.id.bytes[15] = static_cast<u8>(h);
    p.id.bytes[14] = static_cast<u8>(h >> 8);
    p.name = std::move(name);
    p.author = std::move(author);
    p.version = std::move(version);
    p.bucket = version_bucket(p.version);
    p.players = players;
    p.max_players = 100;
    p.flags = wire::entry_flag::online | wire::entry_flag::reachable | (pwd ? wire::entry_flag::has_password : 0);
    p.region = region;
    p.created_ms = 1'700'000'000'000ULL + h * 1000;
    return p;
}

}  // namespace

TEST_CASE("text search agrees with a brute-force scan and pages correctly", "[search]") {
    std::mt19937 rng(7);
    const char* words[] = {"Arena", "arena", "Late Game", "Solo", "Duos", "Squads", "Creative", "Zone Wars", "ab", "x"};
    const char* authors[] = {"auties", "Bob", "carol", "DAVE"};
    SearchIndex idx;
    std::vector<PubEntry> all;
    for (u32 h = 1; h <= 400; ++h) {
        auto p = make(h, std::string(words[rng() % 10]) + " " + std::to_string(rng() % 50), authors[rng() % 4],
                      rng() % 30, rng() % 3 == 0, static_cast<wire::Region>(rng() % 3 + 3), rng() % 2 ? "4.5" : "10.40");
        if (rng() % 10 == 0) p.flags &= ~wire::entry_flag::online;
        idx.upsert(p);
        all.push_back(p);
    }
    // Rename some entries and delete others to exercise index maintenance.
    for (u32 i = 0; i < 60; ++i) {
        auto& p = all[rng() % all.size()];
        p.name = std::string(words[rng() % 10]) + " renamed";
        idx.upsert(p);
    }
    for (u32 i = 0; i < 30; ++i) {
        const u32 h = rng() % 400 + 1;
        idx.remove(h);
        all[h - 1].flags = 0;  // treat as gone
    }

    for (std::string q : {"arena", "ARENA", "renamed", "ab", "a", "zone w", "bob", "auties", "nothing here", "00000000"}) {
        for (auto sort : {wire::Sort::players, wire::Sort::newest, wire::Sort::name}) {
            for (auto pwd : {wire::PasswordFilter::any, wire::PasswordFilter::none, wire::PasswordFilter::only}) {
                const wire::ViewSpec spec{.bucket = 0, .password = pwd, .region = wire::Region::all, .sort = sort};
                std::vector<wire::ListEntry> expect;
                const std::string fq = fold(q);
                for (const auto& p : all) {
                    if (!visible_flags(p.flags)) continue;
                    const bool has = p.flags & wire::entry_flag::has_password;
                    if ((pwd == wire::PasswordFilter::none && has) || (pwd == wire::PasswordFilter::only && !has)) continue;
                    const std::string text = fold(p.name) + "\x1f" + fold(p.author) + "\x1f" + p.id.to_string();
                    if (text.find(fq) == std::string::npos) continue;
                    p.to_list_entry(expect.emplace_back());
                }
                std::sort(expect.begin(), expect.end(), [&](auto& a, auto& b) { return client::ViewMirror::less(sort, a, b); });

                std::vector<wire::ListEntry> got;
                wire::Bytes cursor;
                u32 total = 0;
                do {
                    bool bad = false;
                    auto res = idx.query(spec, q, 7, cursor, bad);
                    REQUIRE_FALSE(bad);
                    if (cursor.empty()) total = res.total;
                    got.insert(got.end(), res.entries.begin(), res.entries.end());
                    cursor = res.next_cursor;
                } while (!cursor.empty());
                INFO("query '" << q << "'");
                CHECK(total == expect.size());
                REQUIRE(got.size() == expect.size());
                for (std::size_t i = 0; i < got.size(); ++i) CHECK(got[i].handle == expect[i].handle);
            }
        }
    }
}
