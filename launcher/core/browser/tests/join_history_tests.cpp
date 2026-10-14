#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

#include "reboot/browser/join_history.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/testing/event_recorder.hpp"

using namespace rb;
using namespace rb::browser;
using namespace std::chrono_literals;

namespace {

[[nodiscard]] ServerId server_id(u8 seed) {
    Uuid id;
    id.bytes.fill(seed);
    return ServerId{id};
}

[[nodiscard]] JoinTarget server(u8 seed, std::string name) {
    return JoinTarget{ServerTarget{server_id(seed), std::move(name), "author"}};
}

[[nodiscard]] JoinTarget address(std::string text, std::string host, u16 port) {
    return JoinTarget{AddressTarget{std::move(text), HostPort{std::move(host), Port{port}}}};
}

struct Fixture {
    explicit Fixture(std::vector<JoinHistoryEntry> loaded = {})
        : history(clock, events, std::move(loaded), [this](const std::vector<JoinHistoryEntry>& entries) {
              persisted.push_back(entries);
          }) {}

    [[nodiscard]] std::vector<JoinTarget> targets() const {
        std::vector<JoinTarget> out;
        for (const JoinHistoryEntry& entry : history.entries()) out.push_back(entry.target);
        return out;
    }

    ManualClock clock;
    EventBus events{EngineEpoch{1}};
    testing::EventRecorder recorder{events, EventFilter{.kinds = {EventKind::JoinHistoryChanged}}};
    std::vector<std::vector<JoinHistoryEntry>> persisted;
    JoinHistory history;
};

}  // namespace

TEST_CASE("a join goes to the top, and a repeat moves its place there with the newer name", "[browser][history]") {
    Fixture f;
    f.history.record(server(1, "Arena"));
    f.clock.advance(1min);
    f.history.record(address("play.example", "play.example", 7777));
    f.clock.advance(1min);
    f.history.record(server(1, "Arena 2"));
    CHECK(f.targets() == std::vector{server(1, "Arena 2"), address("play.example", "play.example", 7777)});
    CHECK(f.history.entries().front().last_joined == f.clock.system_now());

    // The same host and port typed another way is the same place.
    f.history.record(address("PLAY.example:7777", "PLAY.example", 7777));
    CHECK(f.targets() == std::vector{address("PLAY.example:7777", "PLAY.example", 7777), server(1, "Arena 2")});

    REQUIRE(f.persisted.size() == 4);
    CHECK(f.persisted.back() == f.history.entries());
    f.recorder.pump();
    const auto changes = f.recorder.payloads<JoinHistoryChanged>(EventKind::JoinHistoryChanged);
    REQUIRE_FALSE(changes.empty());
    CHECK(changes.back()->entries == f.history.entries());
}

TEST_CASE("only the newest joins are kept", "[browser][history]") {
    Fixture f;
    for (u8 seed = 0; seed < JoinHistory::kMaxEntries + 5; ++seed) {
        f.history.record(server(seed, "server"));
        f.clock.advance(1s);
    }
    REQUIRE(f.history.entries().size() == JoinHistory::kMaxEntries);
    CHECK(f.history.entries().front().target == server(JoinHistory::kMaxEntries + 4, "server"));
    CHECK(f.history.entries().back().target == server(5, "server"));
}

TEST_CASE("an entry is removed by its place, and clear empties the history", "[browser][history]") {
    Fixture f;
    f.history.record(server(1, "Arena"));
    f.history.record(address("10.0.0.2", "10.0.0.2", 7777));
    CHECK(f.history.remove(server(1, "another name")));
    CHECK_FALSE(f.history.remove(server(1, "Arena")));
    CHECK_FALSE(f.history.remove(address("10.0.0.2:7778", "10.0.0.2", 7778)));
    CHECK(f.targets() == std::vector{address("10.0.0.2", "10.0.0.2", 7777)});

    const std::size_t writes = f.persisted.size();
    f.history.clear();
    CHECK(f.history.entries().empty());
    CHECK(f.persisted.size() == writes + 1);
    f.history.clear();
    CHECK(f.persisted.size() == writes + 1);
}

TEST_CASE("a loaded history is ordered and bounded without a write", "[browser][history]") {
    const auto at = std::chrono::system_clock::time_point{} + 1h;
    Fixture f({JoinHistoryEntry{server(1, "old"), at}, JoinHistoryEntry{server(2, "new"), at + 1min},
               JoinHistoryEntry{server(1, "older copy"), at - 1min}});
    CHECK(f.targets() == std::vector{server(2, "new"), server(1, "old")});
    CHECK(f.persisted.empty());
}
