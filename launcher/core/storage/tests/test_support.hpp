#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/fake_platform_paths.hpp"
#include "reboot/testing/golden.hpp"
#include "reboot/testing/in_memory_file_system.hpp"

namespace reboot::storage::test {

// The strand beside a real WorkerPool: the test thread runs what workers posted until done.
class WorkerStrand final : public Executor {
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

// 2026-10-07T23:58:29Z, so quarantine names are stable.
inline void set_test_time(ManualClock& clock) {
    clock.set_system(std::chrono::system_clock::time_point{std::chrono::seconds{1791417509}});
}

[[nodiscard]] inline NativePath data_file(std::string_view name) { return NativePath(REBOOT_STORAGE_TEST_DATA) / name; }

[[nodiscard]] inline std::string golden_text(std::string_view name) {
    const Result<std::vector<u8>> bytes = testing::read_golden(data_file(name));
    REQUIRE(bytes);
    return {bytes->begin(), bytes->end()};
}

}  // namespace reboot::storage::test
