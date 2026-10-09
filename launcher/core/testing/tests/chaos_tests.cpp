#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/events.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/testing/chaos.hpp"
#include "reboot/testing/deterministic_runtime.hpp"
#include "reboot/testing/event_recorder.hpp"
#include "reboot/testing/manual_waiter.hpp"

using namespace reboot;
using namespace reboot::testing;
using namespace std::chrono_literals;

namespace {

void require_passed(const ConformanceReport& report) {
    INFO(report.describe());
    REQUIRE(report.passed());
}

struct Changed {
    int value = 0;
};

}  // namespace

TEST_CASE("race_all_orderings runs every ordering in each mode", "[testing][chaos]") {
    auto orders = std::make_shared<std::vector<std::string>>();
    const ConformanceReport report = race_all_orderings("three", [orders](DeterministicRuntime&) {
        RaceCase race;
        auto seen = std::make_shared<std::string>();
        for (const char* name : {"a", "b", "c"})
            race.steps.push_back({name, [seen, name] { *seen += name; }});
        race.check = [orders, seen](std::span<const std::string> order, ConformanceReport& check) {
            std::string expected;
            for (const std::string& step : order) expected += step;
            check.expect("steps ran in the reported order", *seen == expected);
            orders->push_back(expected);
        };
        return race;
    });
    require_passed(report);
    CHECK(orders->size() == 12);
}

TEST_CASE("race_all_orderings refuses too many steps", "[testing][chaos]") {
    const ConformanceReport report = race_all_orderings("seven", [](DeterministicRuntime&) {
        RaceCase race;
        for (int i = 0; i < 7; ++i) race.steps.push_back({std::to_string(i), [] {}});
        return race;
    });
    CHECK_FALSE(report.passed());
}

TEST_CASE("Settled mode lets continuations land before the next contender", "[testing][chaos]") {
    const std::array<RaceMode, 1> settled{RaceMode::Settled};
    const ConformanceReport report = race_all_orderings(
        "settled",
        [](DeterministicRuntime& runtime) {
            RaceCase race;
            auto log = std::make_shared<std::string>();
            race.steps.push_back({"post", [&runtime, log] {
                                      *log += "p";
                                      runtime.strand().post([log] { *log += "c"; });
                                  }});
            race.steps.push_back({"next", [log] { *log += "n"; }});
            race.check = [log](std::span<const std::string> order, ConformanceReport& check) {
                check.expect("the continuation ran right after its step",
                             order.front() == "post" ? *log == "pcn" : *log == "npc");
            };
            return race;
        },
        settled);
    require_passed(report);
}

TEST_CASE("operations complete exactly once whatever the order", "[testing][chaos]") {
    using enum OpContender;
    require_passed(race_operation_outcome(OpKind::HttpSmall, DisconnectPolicy::BoundToConnection,
                                          std::array{Complete, Cancel, Deadline}));
    require_passed(race_operation_outcome(OpKind::HttpSmall, DisconnectPolicy::BoundToConnection,
                                          std::array{Fail, ConnectionClosed, Complete}));
    require_passed(race_operation_outcome(OpKind::Play, DisconnectPolicy::Detached, std::array{Complete, ConnectionClosed}));
    require_passed(race_operation_outcome(OpKind::HttpSmall, DisconnectPolicy::Detached,
                                          std::array{AwaitingUser, Deadline, Progress, Complete}));
    require_passed(race_operation_outcome(OpKind::Install, DisconnectPolicy::Detached,
                                          std::array{Deadline, Progress, AwaitingUser, Cancel}));
    require_passed(race_operation_outcome(OpKind::GameControlHello, DisconnectPolicy::Detached,
                                          std::array{AwaitingUser, Deadline}));
    require_passed(race_operation_outcome(OpKind::HttpSmall, DisconnectPolicy::Detached, std::array{Complete, Complete}));
}

TEST_CASE("one answer to a user request wins by CAS", "[testing][chaos]") {
    require_passed(race_user_request(3, false));
    require_passed(race_user_request(2, true));
    require_passed(race_user_request(0, true));
}

TEST_CASE("EventRecorder sees coalescing, drops and the Resync path", "[testing][events]") {
    DeterministicRuntime runtime;
    EventRecorder everything(runtime.events());
    EventRecorder small(runtime.events(), EventFilter{{EventKind::SettingsChanged, EventKind::UserActionResolved}, {}, {}},
                        sizeof(Changed) * 2);

    runtime.events().publish(EventKind::SettingsChanged, Changed{1});
    runtime.events().publish(EventKind::SettingsChanged, Changed{2});
    CHECK(everything.pump() == 1);
    REQUIRE(everything.payloads<Changed>(EventKind::SettingsChanged).size() == 1);
    CHECK(everything.payloads<Changed>(EventKind::SettingsChanged).front()->value == 2);

    for (int i = 0; i < 4; ++i) runtime.events().publish(EventKind::UserActionResolved, Changed{i});
    small.pump();
    CHECK(small.resyncs() == 1);

    for (int i = 0; i < 3; ++i) runtime.events().publish(EventKind::LogLine, Changed{i});
    everything.pump();
    CHECK(everything.count(EventKind::LogLine) == 3);
    CHECK(everything.dropped() == 0);
    CHECK(everything.sequence_ok());
    everything.clear();
    CHECK(everything.events().empty());
}

TEST_CASE("ManualWaiter spends manual time, not real time", "[testing][runtime]") {
    DeterministicRuntime runtime;
    ManualWaiter waiter(runtime);
    bool fired = false;
    auto timer = runtime.timers().after(30s, [&fired] { fired = true; });
    CHECK_FALSE(waiter.wait_until([&fired] { return fired; }, 10s));
    CHECK(waiter.wait_until([&fired] { return fired; }, 30s));
    CHECK(runtime.clock().steady_now().time_since_epoch() >= 30s);
}
