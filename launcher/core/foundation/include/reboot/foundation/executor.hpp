#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <utility>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/clock.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {

using SteadyTime = std::chrono::steady_clock::time_point;

class Executor {
public:
    virtual ~Executor() = default;
    virtual void post(UniqueFunction<void()> task) = 0;
    // Runs `task` no earlier than `when` on this executor's clock.
    virtual void post_at(SteadyTime when, UniqueFunction<void()> task) = 0;
};

// The engine's single domain thread. The engine calls run() on the thread it dedicates to it.
class Strand final : public Executor {
public:
    Strand();
    ~Strand() override;
    Strand(const Strand&) = delete;
    Strand& operator=(const Strand&) = delete;

    void post(UniqueFunction<void()> task) override;
    void post_at(SteadyTime when, UniqueFunction<void()> task) override;

    void run();
    void stop();
    [[nodiscard]] bool running_in_this_thread() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class WorkerPool {
public:
    explicit WorkerPool(std::size_t threads);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    // Runs `work` on a worker and posts its result to `reply_to`. An escaped exception
    // becomes internal.bug.
    template <class T>
    void submit(UniqueFunction<Result<T>(CancelToken)> work, CancelToken token, Executor& reply_to,
                UniqueFunction<void(Result<T>)> done) {
        enqueue([work = std::move(work), token = std::move(token), &reply_to, done = std::move(done)]() mutable {
            Result<T> result = run_guarded(work, token);
            reply_to.post([result = std::move(result), done = std::move(done)]() mutable { done(std::move(result)); });
        });
    }

    void shutdown();

private:
    template <class T>
    static Result<T> run_guarded(UniqueFunction<Result<T>(CancelToken)>& work, const CancelToken& token) {
        try {
            return work(token);
        } catch (...) {
            return std::unexpected(internal_bug("worker_pool"));
        }
    }

    void enqueue(UniqueFunction<void()> job);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class TimerService;

namespace detail {
struct TimerState;
}

// Cancels its timer on destruction; harmless once the service is gone.
class TimerHandle {
public:
    TimerHandle() = default;
    TimerHandle(TimerHandle&& other) noexcept;
    TimerHandle& operator=(TimerHandle&& other) noexcept;
    TimerHandle(const TimerHandle&) = delete;
    TimerHandle& operator=(const TimerHandle&) = delete;
    ~TimerHandle();

    void cancel();
    [[nodiscard]] bool active() const;

private:
    friend class TimerService;
    TimerHandle(std::weak_ptr<detail::TimerState> state, u64 id) : state_(std::move(state)), id_(id) {}

    std::weak_ptr<detail::TimerState> state_;
    u64 id_ = 0;
};

// Strand-only. Callbacks run on the executor given at construction.
class TimerService {
public:
    TimerService(IClock& clock, Executor& executor);
    ~TimerService();
    TimerService(const TimerService&) = delete;
    TimerService& operator=(const TimerService&) = delete;

    [[nodiscard]] TimerHandle after(std::chrono::steady_clock::duration delay, UniqueFunction<void()> callback);
    [[nodiscard]] TimerHandle at(SteadyTime when, UniqueFunction<void()> callback);

private:
    IClock& clock_;
    Executor& executor_;
    // Shared with posted tasks and handles, so either outliving the service is a no-op.
    std::shared_ptr<detail::TimerState> state_;
};

// Test executor: nothing runs until the test says so, and timed tasks follow the ManualClock.
class ManualExecutor final : public Executor {
public:
    explicit ManualExecutor(ManualClock& clock) : clock_(clock) {}

    void post(UniqueFunction<void()> task) override;
    void post_at(SteadyTime when, UniqueFunction<void()> task) override;

    // Due timed tasks count as ready; run_all and advance return how many tasks ran. advance()
    // moves the clock from one due time to the next, so each timed task runs at its own deadline.
    bool run_one();
    std::size_t run_all();
    std::size_t advance(std::chrono::steady_clock::duration by);

private:
    void promote_due();

    ManualClock& clock_;
    std::deque<UniqueFunction<void()>> ready_;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed_;
};

}  // namespace rb
