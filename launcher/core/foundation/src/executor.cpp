#include "reboot/foundation/executor.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "reboot/foundation/log.hpp"

namespace reboot {

namespace {

// Entry points never let an exception escape; a task that throws is a bug, logged and dropped.
void run_guarded_task(UniqueFunction<void()>& task, std::string_view where) {
    try {
        task();
    } catch (...) {
        REBOOT_LOG_ERROR(Engine, "internal.bug: a task threw on the {}", where);
    }
}

}  // namespace

struct Strand::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<UniqueFunction<void()>> ready;
    std::multimap<SteadyTime, UniqueFunction<void()>> timed;
    bool stopped = false;
    std::atomic<std::thread::id> runner{};
};

Strand::Strand() : impl_(std::make_unique<Impl>()) {}

Strand::~Strand() = default;

void Strand::post(UniqueFunction<void()> task) {
    {
        const std::lock_guard lock(impl_->mutex);
        if (impl_->stopped) return;
        impl_->ready.push_back(std::move(task));
    }
    impl_->wake.notify_one();
}

void Strand::post_at(SteadyTime when, UniqueFunction<void()> task) {
    {
        const std::lock_guard lock(impl_->mutex);
        if (impl_->stopped) return;
        impl_->timed.emplace(when, std::move(task));
    }
    impl_->wake.notify_one();
}

void Strand::run() {
    impl_->runner.store(std::this_thread::get_id());
    std::unique_lock lock(impl_->mutex);
    while (!impl_->stopped) {
        const SteadyTime now = std::chrono::steady_clock::now();
        while (!impl_->timed.empty() && impl_->timed.begin()->first <= now) {
            impl_->ready.push_back(std::move(impl_->timed.begin()->second));
            impl_->timed.erase(impl_->timed.begin());
        }
        if (impl_->ready.empty()) {
            if (impl_->timed.empty()) impl_->wake.wait(lock);
            else impl_->wake.wait_until(lock, impl_->timed.begin()->first);
            continue;
        }
        UniqueFunction<void()> task = std::move(impl_->ready.front());
        impl_->ready.pop_front();
        lock.unlock();
        run_guarded_task(task, "strand");
        task = nullptr;
        lock.lock();
    }
    impl_->runner.store(std::thread::id{});
}

void Strand::stop() {
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->stopped = true;
    }
    impl_->wake.notify_all();
}

bool Strand::running_in_this_thread() const { return impl_->runner.load() == std::this_thread::get_id(); }

struct WorkerPool::Impl {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<UniqueFunction<void()>> jobs;
    bool stopping = false;
    std::vector<std::thread> threads;

    void work() {
        while (true) {
            UniqueFunction<void()> job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !jobs.empty(); });
                // Queued jobs still run during shutdown, so every submit gets its done callback.
                if (jobs.empty()) return;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            run_guarded_task(job, "worker pool");
        }
    }
};

WorkerPool::WorkerPool(std::size_t threads) : impl_(std::make_unique<Impl>()) {
    impl_->threads.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) impl_->threads.emplace_back([impl = impl_.get()] { impl->work(); });
}

WorkerPool::~WorkerPool() { shutdown(); }

void WorkerPool::enqueue(UniqueFunction<void()> job) {
    {
        const std::lock_guard lock(impl_->mutex);
        if (impl_->stopping) return;
        impl_->jobs.push_back(std::move(job));
    }
    impl_->wake.notify_one();
}

void WorkerPool::shutdown() {
    {
        const std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->wake.notify_all();
    for (std::thread& thread : impl_->threads)
        if (thread.joinable()) thread.join();
    impl_->threads.clear();
}

TimerHandle::TimerHandle(TimerHandle&& other) noexcept
    : service_(std::exchange(other.service_, nullptr)), id_(std::exchange(other.id_, 0)) {}

TimerHandle& TimerHandle::operator=(TimerHandle&& other) noexcept {
    if (this != &other) {
        cancel();
        service_ = std::exchange(other.service_, nullptr);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

TimerHandle::~TimerHandle() { cancel(); }

void TimerHandle::cancel() {
    if (service_ == nullptr) return;
    std::exchange(service_, nullptr)->cancel(id_);
    id_ = 0;
}

bool TimerHandle::active() const { return service_ != nullptr && service_->active(id_); }

struct TimerService::State {
    u64 next_id = 1;
    std::map<u64, UniqueFunction<void()>> pending;
};

TimerService::TimerService(IClock& clock, Executor& executor)
    : clock_(clock), executor_(executor), state_(std::make_shared<State>()) {}

// Callbacks are destroyed outside the map: one may own a TimerHandle that cancels into it.
TimerService::~TimerService() { [[maybe_unused]] const auto pending = std::exchange(state_->pending, {}); }

TimerHandle TimerService::after(std::chrono::steady_clock::duration delay, UniqueFunction<void()> callback) {
    return at(clock_.steady_now() + delay, std::move(callback));
}

TimerHandle TimerService::at(SteadyTime when, UniqueFunction<void()> callback) {
    const u64 id = state_->next_id++;
    state_->pending.emplace(id, std::move(callback));
    executor_.post_at(when, [state = state_, id] {
        const auto it = state->pending.find(id);
        if (it == state->pending.end()) return;
        UniqueFunction<void()> fire = std::move(it->second);
        state->pending.erase(it);
        fire();
    });
    return TimerHandle(*this, id);
}

void TimerService::cancel(u64 id) {
    const auto it = state_->pending.find(id);
    if (it == state_->pending.end()) return;
    [[maybe_unused]] const UniqueFunction<void()> removed = std::move(it->second);
    state_->pending.erase(it);
}

bool TimerService::active(u64 id) const { return state_->pending.contains(id); }

void ManualExecutor::post(UniqueFunction<void()> task) { ready_.push_back(std::move(task)); }

void ManualExecutor::post_at(SteadyTime when, UniqueFunction<void()> task) { timed_.emplace(when, std::move(task)); }

void ManualExecutor::promote_due() {
    const SteadyTime now = clock_.steady_now();
    while (!timed_.empty() && timed_.begin()->first <= now) {
        ready_.push_back(std::move(timed_.begin()->second));
        timed_.erase(timed_.begin());
    }
}

bool ManualExecutor::run_one() {
    promote_due();
    if (ready_.empty()) return false;
    UniqueFunction<void()> task = std::move(ready_.front());
    ready_.pop_front();
    task();
    return true;
}

std::size_t ManualExecutor::run_all() {
    std::size_t ran = 0;
    while (run_one()) ++ran;
    return ran;
}

std::size_t ManualExecutor::advance(std::chrono::steady_clock::duration by) {
    clock_.advance(by);
    return run_all();
}

}  // namespace reboot
