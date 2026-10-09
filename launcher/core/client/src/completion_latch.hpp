#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace rb::client {

// Covers no capability ids. One value from a callback to a blocked export; deadlines are the executor's.
template <class T>
class CompletionLatch {
public:
    // Notifies under the lock, so the waiter cannot destroy the latch before set() returns.
    void set(T value) {
        std::lock_guard lock(mutex_);
        value_.emplace(std::move(value));
        ready_.notify_all();
    }

    [[nodiscard]] T wait() {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return value_.has_value(); });
        return std::move(*value_);
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<T> value_;
};

}  // namespace rb::client
