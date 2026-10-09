#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"

using namespace rb;
using namespace std::chrono_literals;

TEST_CASE("ManualExecutor runs nothing until pumped, in post order", "[foundation][executor]") {
    ManualClock clock;
    ManualExecutor executor(clock);
    std::vector<int> order;
    executor.post([&] { order.push_back(1); });
    executor.post([&] {
        order.push_back(2);
        executor.post([&] { order.push_back(3); });
    });
    CHECK(order.empty());
    CHECK(executor.run_one());
    CHECK(order == std::vector<int>{1});
    CHECK(executor.run_all() == 2);
    CHECK(order == std::vector<int>{1, 2, 3});
    CHECK_FALSE(executor.run_one());
}

TEST_CASE("ManualExecutor runs timed tasks at their own due time", "[foundation][executor]") {
    ManualClock clock;
    ManualExecutor executor(clock);
    const SteadyTime start = clock.steady_now();
    std::vector<std::chrono::steady_clock::duration> fired_at;
    executor.post_at(start + 30ms, [&] { fired_at.push_back(clock.steady_now() - start); });
    executor.post_at(start + 10ms, [&] {
        fired_at.push_back(clock.steady_now() - start);
        // Armed by a timer, due inside the same advance.
        executor.post_at(clock.steady_now() + 5ms, [&] { fired_at.push_back(clock.steady_now() - start); });
    });
    executor.post_at(start + 100ms, [&] { fired_at.push_back(clock.steady_now() - start); });

    CHECK(executor.run_all() == 0);
    CHECK(executor.advance(50ms) == 3);
    CHECK(fired_at == std::vector<std::chrono::steady_clock::duration>{10ms, 15ms, 30ms});
    CHECK(clock.steady_now() - start == 50ms);
    CHECK(executor.advance(50ms) == 1);
    CHECK(fired_at.back() == 100ms);
}

TEST_CASE("TimerService fires once and a cancelled timer never fires", "[foundation][executor][timer]") {
    ManualClock clock;
    ManualExecutor executor(clock);
    TimerService timers(clock, executor);
    int fired = 0;
    TimerHandle kept = timers.after(10ms, [&] { ++fired; });
    TimerHandle cancelled = timers.after(10ms, [&] { fired += 10; });
    CHECK(kept.active());
    cancelled.cancel();
    CHECK_FALSE(cancelled.active());
    {
        const TimerHandle dropped = timers.after(5ms, [&] { fired += 100; });
    }
    executor.advance(9ms);
    CHECK(fired == 0);
    executor.advance(1ms);
    CHECK(fired == 1);
    CHECK_FALSE(kept.active());
    executor.advance(1h);
    CHECK(fired == 1);
}

TEST_CASE("Moving a TimerHandle moves the ownership of its timer", "[foundation][executor][timer]") {
    ManualClock clock;
    ManualExecutor executor(clock);
    TimerService timers(clock, executor);
    int fired = 0;
    TimerHandle first = timers.after(10ms, [&] { ++fired; });
    TimerHandle second = std::move(first);
    CHECK_FALSE(first.active());
    CHECK(second.active());
    // Assigning over an active handle cancels the timer it held.
    second = timers.after(20ms, [&] { fired += 10; });
    executor.advance(30ms);
    CHECK(fired == 10);
}

TEST_CASE("A timer callback may cancel its own and other handles", "[foundation][executor][timer]") {
    ManualClock clock;
    ManualExecutor executor(clock);
    TimerService timers(clock, executor);
    TimerHandle self;
    TimerHandle other = timers.after(20ms, [] { FAIL("cancelled timer fired"); });
    bool fired = false;
    self = timers.after(10ms, [&] {
        fired = true;
        self.cancel();
        other.cancel();
    });
    executor.advance(1s);
    CHECK(fired);
}

TEST_CASE("A timer and its handle may outlive the service", "[foundation][executor][timer]") {
    ManualClock clock;
    ManualExecutor executor(clock);
    bool fired = false;
    TimerHandle handle;
    {
        TimerService timers(clock, executor);
        handle = timers.after(10ms, [&] { fired = true; });
        CHECK(handle.active());
    }
    CHECK_FALSE(handle.active());
    executor.advance(1s);
    CHECK_FALSE(fired);
    handle.cancel();
}

TEST_CASE("Strand runs posted and timed work on its own thread and survives a throwing task",
          "[foundation][executor][strand]") {
    Strand strand;
    std::thread runner([&] { strand.run(); });

    std::promise<bool> on_strand;
    strand.post([] { throw std::runtime_error("bug"); });
    strand.post([&] { on_strand.set_value(strand.running_in_this_thread()); });
    CHECK(on_strand.get_future().get());
    CHECK_FALSE(strand.running_in_this_thread());

    std::promise<void> timed;
    strand.post_at(std::chrono::steady_clock::now() + 1ms, [&] { timed.set_value(); });
    timed.get_future().get();

    strand.stop();
    runner.join();
    // Work posted after stop is dropped.
    bool late = false;
    strand.post([&] { late = true; });
    CHECK_FALSE(late);
}

TEST_CASE("WorkerPool replies on the given executor and turns a throw into internal.bug",
          "[foundation][executor][worker]") {
    ManualClock clock;
    ManualExecutor reply_to(clock);
    std::vector<Result<int>> results;
    {
        WorkerPool pool(2);
        pool.submit<int>([](CancelToken) -> Result<int> { return 42; }, {}, reply_to,
                         [&](Result<int> result) { results.push_back(std::move(result)); });
        pool.submit<int>([](CancelToken) -> Result<int> { throw std::runtime_error("boom"); }, {}, reply_to,
                         [&](Result<int> result) { results.push_back(std::move(result)); });
        CancelSource source;
        source.cancel(CancelReason::User);
        pool.submit<int>(
            [](CancelToken token) -> Result<int> { return token.cancelled() ? 1 : 0; }, source.token(), reply_to,
            [&](Result<int> result) { results.push_back(std::move(result)); });
        // Shutdown still runs every queued job, so each submit gets its reply.
        pool.shutdown();
    }
    CHECK(results.empty());
    CHECK(reply_to.run_all() == 3);
    REQUIRE(results.size() == 3);
    int ok = 0;
    int bugs = 0;
    for (const Result<int>& result : results) {
        if (result) {
            ++ok;
            CHECK((*result == 42 || *result == 1));
        } else {
            ++bugs;
            CHECK(result.error().id == "internal.bug");
            CHECK(result.error().domain == ErrorDomain::Internal);
        }
    }
    CHECK(ok == 2);
    CHECK(bugs == 1);
}
