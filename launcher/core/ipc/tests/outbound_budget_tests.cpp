#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "reboot/foundation/clock.hpp"
#include "reboot/ipc/outbound_budget.hpp"

using namespace reboot;
using Verdict = reboot::ipc::OutboundBudget::Verdict;
using std::chrono_literals::operator""s;

TEST_CASE("the budget counts charged bytes until they are refunded", "[ipc][budget]") {
    ManualClock clock;
    ipc::OutboundBudget budget(clock, 100);
    CHECK(budget.fits(100));
    budget.charge(60);
    CHECK(budget.used() == 60);
    CHECK(budget.fits(40));
    CHECK_FALSE(budget.fits(41));
    budget.refund(20);
    CHECK(budget.used() == 40);
    budget.refund(500);
    CHECK(budget.used() == 0);
}

TEST_CASE("the second overflow within the window disconnects", "[ipc][budget]") {
    ManualClock clock;
    ipc::OutboundBudget budget(clock, 10);
    CHECK(budget.overflow() == Verdict::Resync);
    clock.advance(contracts::ipc::kSlowConsumerWindow - 1s);
    CHECK(budget.overflow() == Verdict::Disconnect);
}

TEST_CASE("an overflow after the window is only a Resync again", "[ipc][budget]") {
    ManualClock clock;
    ipc::OutboundBudget budget(clock, 10);
    CHECK(budget.overflow() == Verdict::Resync);
    clock.advance(contracts::ipc::kSlowConsumerWindow);
    CHECK(budget.overflow() == Verdict::Resync);
    clock.advance(1s);
    CHECK(budget.overflow() == Verdict::Disconnect);
}
