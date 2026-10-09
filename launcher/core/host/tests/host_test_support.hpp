#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::host::test {

// The strand beside real worker threads: workers post from their threads, timed tasks follow the
// ManualClock, and only the test thread runs anything.
class TestStrand final : public Executor {
public:
    explicit TestStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        posted_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    // Runs what is ready, including what that posts, without waiting for other threads.
    std::size_t run_ready() {
        std::size_t ran = 0;
        while (UniqueFunction<void()> task = next(std::chrono::milliseconds{0})) {
            task();
            ++ran;
        }
        return ran;
    }

    // Runs tasks, waiting for other threads to post, until `done` holds.
    template <class Done>
    void run_until(Done&& done) {
        run_ready();
        while (!done()) {
            UniqueFunction<void()> task = next(std::chrono::seconds{20});
            REQUIRE(static_cast<bool>(task));
            task();
            run_ready();
        }
    }

    // Moves time one due task at a time, so each timer runs at its own deadline.
    void advance(std::chrono::steady_clock::duration by) {
        const SteadyTime end = clock_.steady_now() + by;
        run_ready();
        while (true) {
            SteadyTime due{};
            {
                const std::scoped_lock lock(mutex_);
                if (timed_.empty() || timed_.begin()->first > end) break;
                due = timed_.begin()->first;
            }
            if (due > clock_.steady_now()) clock_.advance(due - clock_.steady_now());
            run_ready();
        }
        if (end > clock_.steady_now()) clock_.advance(end - clock_.steady_now());
        run_ready();
    }

    // Moves time a step at a time until `done` holds, letting a worker reply land after each step.
    template <class Done>
    [[nodiscard]] bool advance_until(Done&& done, std::chrono::steady_clock::duration by,
                                     std::chrono::steady_clock::duration step = std::chrono::seconds{1}) {
        const SteadyTime end = clock_.steady_now() + by;
        while (!done()) {
            if (clock_.steady_now() >= end) return false;
            advance(step);
            if (UniqueFunction<void()> task = next(std::chrono::milliseconds{20})) {
                task();
                run_ready();
            }
        }
        return true;
    }

private:
    // A bound, not a sleep: a missing post fails the test instead of hanging it.
    UniqueFunction<void()> next(std::chrono::milliseconds wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        if (wait.count() > 0 && !posted_.wait_for(lock, wait, [this] { return !ready_.empty(); })) return {};
        if (ready_.empty()) return {};
        UniqueFunction<void()> task = std::move(ready_.front());
        ready_.pop_front();
        return task;
    }

    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

[[nodiscard]] inline std::string describe(const Diagnostic& diag) {
    std::string out = diag.id;
    for (const Diagnostic& cause : diag.causes) out += " <- " + describe(cause);
    return out;
}

[[nodiscard]] inline HostProfileId profile_id(u8 seed) {
    HostProfileId id;
    id.value.bytes.fill(seed);
    id.value.bytes[6] = 0x40;
    return id;
}

}  // namespace rb::host::test
