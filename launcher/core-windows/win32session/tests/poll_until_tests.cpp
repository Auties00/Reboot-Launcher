#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

#include "poll_until.hpp"

using namespace rb::os_windows::win32session;
using namespace std::chrono_literals;

namespace {

// A clock the test advances by the exact amount each pause is asked to wait, so poll_until runs
// to its bound without real sleeping.
struct FakeClock {
    std::chrono::steady_clock::time_point now{};
    std::vector<std::chrono::milliseconds> waited;

    auto time() { return [this] { return now; }; }
    auto pause() {
        return [this](std::chrono::milliseconds wait) {
            waited.push_back(wait);
            now += wait;
        };
    }
};

}  // namespace

TEST_CASE("a zero bound probes exactly once and never pauses") {
    FakeClock clock;
    int probes = 0;
    const bool ok = poll_until([&] { ++probes; return false; }, clock.pause(), clock.time(), 0ms, 25ms);
    CHECK_FALSE(ok);
    CHECK(probes == 1);
    CHECK(clock.waited.empty());
}

TEST_CASE("a bounded poll stops at its deadline and does not overshoot it") {
    FakeClock clock;
    const bool ok = poll_until([] { return false; }, clock.pause(), clock.time(), 100ms, 25ms);
    CHECK_FALSE(ok);
    // The last wait is clamped to the time left, so the clock lands exactly on the deadline.
    CHECK(clock.now.time_since_epoch() == 100ms);
    std::chrono::milliseconds total{};
    for (auto w : clock.waited) total += w;
    CHECK(total == 100ms);
    for (auto w : clock.waited) CHECK(w <= 25ms);
}

TEST_CASE("a probe that holds returns before the bound") {
    FakeClock clock;
    int probes = 0;
    const bool ok = poll_until([&] { return ++probes == 3; }, clock.pause(), clock.time(), 1000ms, 10ms);
    CHECK(ok);
    CHECK(probes == 3);
    CHECK(clock.waited.size() == 2);  // two pauses between the three probes
}

TEST_CASE("a probe holding on the first try never pauses") {
    FakeClock clock;
    const bool ok = poll_until([] { return true; }, clock.pause(), clock.time(), 1000ms, 10ms);
    CHECK(ok);
    CHECK(clock.waited.empty());
}
