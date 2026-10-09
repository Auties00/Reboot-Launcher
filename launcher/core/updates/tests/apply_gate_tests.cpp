#include <catch2/catch_test_macros.hpp>

#include "reboot/updates/apply_gate.hpp"

using namespace rb;
using namespace rb::updates;

namespace {

ActivitySnapshot busy() {
    ActivitySnapshot snapshot;
    snapshot.live.push_back(LiveActivity{.kind = LiveKind::BackendPin});
    return snapshot;
}

}  // namespace

TEST_CASE("an idle gate opens inside arm", "[updates]") {
    ApplyGate gate;
    int opened = 0;
    gate.arm(ActivitySnapshot{}, [&] { ++opened; });
    CHECK(opened == 1);
    CHECK_FALSE(gate.armed());
}

TEST_CASE("a busy gate opens once activity reaches zero", "[updates]") {
    ApplyGate gate;
    int opened = 0;
    gate.arm(busy(), [&] { ++opened; });
    CHECK(gate.armed());
    CHECK(gate.blocking() == busy().live);

    gate.update(busy());
    CHECK(opened == 0);
    gate.update(ActivitySnapshot{});
    CHECK(opened == 1);
    CHECK(gate.blocking().empty());
    gate.update(ActivitySnapshot{});
    CHECK(opened == 1);
}

TEST_CASE("a disarmed gate never opens and forgets the drain", "[updates]") {
    ApplyGate gate;
    int opened = 0;
    gate.arm(busy(), [&] { ++opened; });
    gate.drain_started(busy());
    REQUIRE(gate.drained());
    gate.disarm();
    gate.update(ActivitySnapshot{});
    CHECK(opened == 0);
    CHECK_FALSE(gate.drained());
}

TEST_CASE("on_open may arm the gate again", "[updates]") {
    ApplyGate gate;
    int opened = 0;
    gate.arm(ActivitySnapshot{}, [&] { gate.arm(busy(), [&] { ++opened; }); });
    CHECK(gate.armed());
    gate.update(ActivitySnapshot{});
    CHECK(opened == 1);
}
