#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace rb::sdk {

// Where a subscription runs its handler. reboot_client wakes it on a library thread that must not
// block, so post() only queues: a UI adapts its loop (WinUI DispatcherQueue::TryEnqueue, GLib
// g_idle_add, Qt QMetaObject::invokeMethod, FTXUI ScreenInteractive::Post). post() is called from
// any thread and must never run `task` inline.
class Dispatcher {
public:
    virtual ~Dispatcher() = default;
    virtual void post(std::function<void()> task) = 0;
};

// A queue the owner drains on its own thread, for a blocking CLI loop or a test.
class QueueDispatcher final : public Dispatcher {
public:
    void post(std::function<void()> task) override {
        {
            const std::scoped_lock lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        posted_.notify_one();
    }

    // Runs the tasks queued so far.
    std::size_t run_pending() {
        std::deque<std::function<void()>> tasks;
        {
            const std::scoped_lock lock(mutex_);
            tasks.swap(tasks_);
        }
        for (std::function<void()>& task : tasks) task();
        return tasks.size();
    }

    // Waits up to `timeout` for a task, then runs everything queued; false when none came.
    bool run_for(std::chrono::milliseconds timeout) {
        {
            std::unique_lock lock(mutex_);
            if (!posted_.wait_for(lock, timeout, [this] { return !tasks_.empty(); })) return false;
        }
        run_pending();
        return true;
    }

    // Runs tasks until `done` holds, or `timeout` passes; returns `done`.
    template <class Done>
    bool run_until(Done&& done, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        run_pending();
        while (!done()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left <= std::chrono::milliseconds::zero()) return done();
            run_for(left);
        }
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<std::function<void()>> tasks_;
};

// Runs tasks on a thread of its own, for a CLI that reacts to events by callback.
class ThreadDispatcher final : public Dispatcher {
public:
    ThreadDispatcher() : thread_([this] { run(); }) {}
    // Runs what is already queued, then stops.
    ~ThreadDispatcher() override {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        posted_.notify_one();
        thread_.join();
    }
    ThreadDispatcher(const ThreadDispatcher&) = delete;
    ThreadDispatcher& operator=(const ThreadDispatcher&) = delete;

    void post(std::function<void()> task) override {
        {
            const std::scoped_lock lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        posted_.notify_one();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                posted_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }

    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<std::function<void()>> tasks_;
    bool stopping_ = false;
    std::thread thread_;
};

}  // namespace rb::sdk
