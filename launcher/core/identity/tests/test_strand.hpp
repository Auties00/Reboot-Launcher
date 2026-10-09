#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"

namespace reboot::identity::test {

// The strand beside a real WorkerPool: the test thread runs what workers posted until done.
class TestStrand final : public Executor {
public:
    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        tasks_.push_back(std::move(task));
        ready_.notify_one();
    }
    void post_at(SteadyTime, UniqueFunction<void()> task) override { post(std::move(task)); }

    template <class Done>
    void run_until(Done&& done) {
        while (!done()) {
            UniqueFunction<void()> task;
            {
                std::unique_lock lock(mutex_);
                // A bound, not a sleep: a missing reply fails the test instead of hanging it.
                REQUIRE(ready_.wait_for(lock, std::chrono::seconds{10}, [this] { return !tasks_.empty(); }));
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<UniqueFunction<void()>> tasks_;
};

}  // namespace reboot::identity::test
