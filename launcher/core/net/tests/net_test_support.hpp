#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::net::test {

// The strand beside real worker or I/O threads: they post from their threads, timed tasks follow
// the ManualClock, and only the test thread runs anything.
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
        while (UniqueFunction<void()> task = next(false)) {
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
            UniqueFunction<void()> task = next(true);
            REQUIRE(static_cast<bool>(task));
            task();
            run_ready();
        }
    }

    [[nodiscard]] std::size_t timed_pending() {
        const std::scoped_lock lock(mutex_);
        return timed_.size();
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

private:
    UniqueFunction<void()> next(bool wait) {
        std::unique_lock lock(mutex_);
        const SteadyTime now = clock_.steady_now();
        while (!timed_.empty() && timed_.begin()->first <= now) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        // A bound, not a sleep: a missing post fails the test instead of hanging it.
        if (wait && !posted_.wait_for(lock, std::chrono::seconds{20}, [this] { return !ready_.empty(); })) return {};
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

[[nodiscard]] inline std::string arg_text(const Diagnostic& diag, std::string_view name) {
    const Arg* arg = diag.find_arg(name);
    if (arg == nullptr) return {};
    if (const auto* text = std::get_if<std::string>(arg)) return *text;
    if (const auto* number = std::get_if<u64>(arg)) return std::to_string(*number);
    if (const auto* number = std::get_if<i64>(arg)) return std::to_string(*number);
    if (const auto* flag = std::get_if<bool>(arg)) return *flag ? "true" : "false";
    if (const auto* limit = std::get_if<std::chrono::milliseconds>(arg)) return std::to_string(limit->count()) + "ms";
    if (const auto* path = std::get_if<WirePath>(arg)) return path->display;
    return {};
}

[[nodiscard]] inline std::vector<u8> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

}  // namespace rb::net::test
