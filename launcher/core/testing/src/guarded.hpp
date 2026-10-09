#pragma once

#include <memory>
#include <mutex>
#include <utility>

namespace reboot::testing {

// Callback results, written on a port's thread and read on the waiting one.
template <class T>
class Guarded {
public:
    template <class F>
    decltype(auto) with(F&& f) {
        const std::scoped_lock lock(mutex_);
        return std::forward<F>(f)(value_);
    }
    [[nodiscard]] T copy() const {
        const std::scoped_lock lock(mutex_);
        return value_;
    }

private:
    mutable std::mutex mutex_;
    T value_{};
};

template <class T>
using Shared = std::shared_ptr<Guarded<T>>;

template <class T>
[[nodiscard]] Shared<T> make_shared_state() {
    return std::make_shared<Guarded<T>>();
}

}  // namespace reboot::testing
