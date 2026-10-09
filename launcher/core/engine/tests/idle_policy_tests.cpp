#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "reboot/engine/idle_policy.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"

using namespace rb;
using namespace rb::engine;
using namespace std::chrono_literals;

namespace {

struct Fixture {
    explicit Fixture(EngineOrigin origin) : strand(clock), timers(clock, strand), policy(timers, origin, [this] { ++exits; }) {}

    ManualClock clock;
    ManualExecutor strand;
    TimerService timers;
    int exits = 0;
    IdlePolicy policy;
};

}  // namespace

TEST_CASE("idle policy: an on-demand engine exits five minutes after the last work and client are gone") {
    Fixture f(EngineOrigin::OnDemand);
    f.policy.update(1, true);
    f.strand.advance(10min);
    CHECK(f.exits == 0);

    f.policy.update(0, false);
    CHECK(f.policy.exit_pending());
    f.strand.advance(IdlePolicy::kIdleExitDelay - 1s);
    CHECK(f.exits == 0);
    f.strand.advance(1s);
    CHECK(f.exits == 1);
}

TEST_CASE("idle policy: going busy again cancels a pending exit") {
    Fixture f(EngineOrigin::OnDemand);
    f.policy.update(0, false);
    f.strand.advance(4min);
    f.policy.update(1, false);
    CHECK_FALSE(f.policy.exit_pending());
    f.strand.advance(10min);
    CHECK(f.exits == 0);

    f.policy.update(0, false);
    f.strand.advance(4min);
    f.policy.update(0, true);
    f.strand.advance(10min);
    CHECK(f.exits == 0);
}

TEST_CASE("idle policy: resident engines never exit on their own") {
    for (const EngineOrigin origin : {EngineOrigin::ServiceManager, EngineOrigin::Foreground}) {
        Fixture f(origin);
        f.policy.update(0, false);
        CHECK_FALSE(f.policy.exit_pending());
        f.strand.advance(1h);
        CHECK(f.exits == 0);
    }
}

TEST_CASE("idle policy: a work-idle waiter ignores connected clients and fires once") {
    Fixture f(EngineOrigin::ServiceManager);
    int fired = 0;
    f.policy.update(3, true);
    f.policy.when_work_idle([&fired] { ++fired; });
    CHECK(fired == 0);
    f.policy.update(3, false);
    CHECK(fired == 1);
    f.policy.update(3, true);
    f.policy.update(3, false);
    CHECK(fired == 1);
}

TEST_CASE("idle policy: a waiter fires at once when no work is live") {
    Fixture f(EngineOrigin::OnDemand);
    f.policy.update(2, false);
    int fired = 0;
    f.policy.when_work_idle([&fired] { ++fired; });
    CHECK(fired == 1);
}

TEST_CASE("idle policy: a disabled policy fires nothing") {
    Fixture f(EngineOrigin::OnDemand);
    int fired = 0;
    f.policy.update(0, true);
    f.policy.when_work_idle([&fired] { ++fired; });
    f.policy.disable();
    f.policy.update(0, false);
    f.strand.advance(1h);
    CHECK(fired == 0);
    CHECK(f.exits == 0);
    CHECK_FALSE(f.policy.exit_pending());
}
