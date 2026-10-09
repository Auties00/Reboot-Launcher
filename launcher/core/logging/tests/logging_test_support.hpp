#pragma once

#include <chrono>
#include <condition_variable>
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
#include "reboot/foundation/log.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/logging/log_file_names.hpp"

namespace reboot::logging::test {

// 2026-10-08T13:05:09Z, pid 4242.
inline const LogFileGroup kGroup{std::chrono::sys_days{std::chrono::year{2026} / 10 / 8} + std::chrono::hours{13} +
                                     std::chrono::minutes{5} + std::chrono::seconds{9},
                                 4242};

[[nodiscard]] inline SessionId session_of(u8 fill) {
    SessionId session;
    session.value.bytes.fill(fill);
    return session;
}

[[nodiscard]] inline LogRecord record_of(std::string text, LogCategory category = LogCategory::Engine,
                                         std::optional<SessionId> session = std::nullopt) {
    return LogRecord{std::chrono::system_clock::time_point{kGroup.started_at}, LogLevel::Info, category, session,
                     std::move(text)};
}

[[nodiscard]] inline Diagnostic fault(std::string id = "test.fault") {
    Diagnostic diag;
    diag.id = std::move(id);
    return diag;
}

// The strand beside a real WorkerPool: post() is thread-safe and the test thread runs what workers
// posted. Timed tasks wait for advance(), so op deadlines follow the ManualClock.
class WorkerStrand final : public Executor {
public:
    explicit WorkerStrand(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        ready_.push_back(std::move(task));
        wake_.notify_one();
    }
    void post_at(SteadyTime when, UniqueFunction<void()> task) override {
        const std::scoped_lock lock(mutex_);
        timed_.emplace(when, std::move(task));
    }

    template <class Done>
    void run_until(Done&& done) {
        while (!done()) {
            UniqueFunction<void()> task;
            {
                std::unique_lock lock(mutex_);
                // A bound, not a sleep: a missing reply fails the test instead of hanging it.
                REQUIRE(wake_.wait_for(lock, std::chrono::seconds{20}, [this] { return !ready_.empty(); }));
                task = std::move(ready_.front());
                ready_.pop_front();
            }
            task();
        }
    }

    void advance(std::chrono::steady_clock::duration by) {
        clock_.advance(by);
        const std::scoped_lock lock(mutex_);
        while (!timed_.empty() && timed_.begin()->first <= clock_.steady_now()) {
            ready_.push_back(std::move(timed_.begin()->second));
            timed_.erase(timed_.begin());
        }
        wake_.notify_one();
    }

private:
    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

}  // namespace reboot::logging::test
