#pragma once

#include <any>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::integration::test {

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

    // Runs what is ready now, without waiting for workers.
    void run_ready() {
        for (;;) {
            UniqueFunction<void()> task;
            {
                const std::scoped_lock lock(mutex_);
                if (ready_.empty()) return;
                task = std::move(ready_.front());
                ready_.pop_front();
            }
            task();
        }
    }

    void advance(std::chrono::steady_clock::duration by) {
        clock_.advance(by);
        {
            const std::scoped_lock lock(mutex_);
            while (!timed_.empty() && timed_.begin()->first <= clock_.steady_now()) {
                ready_.push_back(std::move(timed_.begin()->second));
                timed_.erase(timed_.begin());
            }
        }
        run_ready();
    }

private:
    ManualClock& clock_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

// Declared after the fixture, it opens on destruction too, so a failed REQUIRE never leaves a worker
// blocked while the pool joins.
class Gate {
public:
    Gate() = default;
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    ~Gate() { open(); }

    [[nodiscard]] std::shared_future<void> future() const { return future_; }
    void open() {
        if (opened_) return;
        opened_ = true;
        promise_.set_value();
    }

private:
    std::promise<void> promise_;
    std::shared_future<void> future_ = promise_.get_future().share();
    bool opened_ = false;
};

// Clock, strand, timers, events and ops wired as the engine wires them.
struct OpRig {
    OpRig() : strand(clock), timers(clock, strand), events(EngineEpoch{1}), ops(clock, timers, events) {}

    [[nodiscard]] ErasedOutcome wait(OpId op) {
        strand.run_until([&] { return ops.outcome(op).has_value(); });
        return *ops.outcome(op);
    }

    ManualClock clock;
    WorkerStrand strand;
    TimerService timers;
    EventBus events;
    OpRegistry ops;
};

template <class T>
[[nodiscard]] T completed_value(const ErasedOutcome& outcome) {
    const auto* completed = std::get_if<Completed<std::any>>(&outcome);
    REQUIRE(completed != nullptr);
    const T* value = std::any_cast<T>(&completed->value);
    REQUIRE(value != nullptr);
    return *value;
}

[[nodiscard]] inline Diagnostic failure_of(const ErasedOutcome& outcome) {
    const auto* failed = std::get_if<Failed>(&outcome);
    REQUIRE(failed != nullptr);
    return failed->error;
}

[[nodiscard]] inline Diagnostic fault(std::string id = "test.fault", ErrorKind kind = ErrorKind::Generic) {
    Diagnostic diag;
    diag.id = std::move(id);
    diag.kind = kind;
    return diag;
}

}  // namespace reboot::integration::test
