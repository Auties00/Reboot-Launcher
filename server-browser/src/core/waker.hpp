#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "core/features.hpp"
#include "core/types.hpp"

#if defined(__linux__)
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace sb {

// Sleep/wake handshake between one consumer and many producers. Producers only pay a
// syscall when the consumer actually announced it is going to sleep.
// Consumer protocol: announce_sleep(); re-check for work; if found cancel_sleep(), else block.
class Waker {
public:
    Waker() {
#if defined(__linux__)
        efd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        SB_ASSERT(efd_ >= 0);
#endif
    }
    Waker(const Waker&) = delete;
    Waker& operator=(const Waker&) = delete;
    ~Waker() {
#if defined(__linux__)
        ::close(efd_);
#endif
    }

    // Linux only: lets an epoll loop wait on the waker.
    [[nodiscard]] int fd() const noexcept { return efd_; }

    void announce_sleep() noexcept { state_.store(kSleeping, std::memory_order_seq_cst); }
    void cancel_sleep() noexcept { state_.store(kAwake, std::memory_order_relaxed); }

    // Blocks until woken or the timeout elapses; a negative timeout blocks indefinitely.
    void block(std::chrono::milliseconds timeout = std::chrono::milliseconds(-1)) noexcept {
#if defined(__linux__)
        pollfd p{efd_, POLLIN, 0};
        (void)::poll(&p, 1, static_cast<int>(timeout.count()));
        drain();
#else
        std::unique_lock lk(mu_);
        auto awake = [&] { return state_.load(std::memory_order_acquire) != kSleeping; };
        if (timeout.count() < 0) cv_.wait(lk, awake);
        else cv_.wait_for(lk, timeout, awake);
        state_.store(kAwake, std::memory_order_relaxed);
#endif
    }

    // Resets the eventfd counter after an epoll wakeup.
    void drain() noexcept {
#if defined(__linux__)
        eventfd_t v;
        (void)::eventfd_read(efd_, &v);
#endif
        state_.store(kAwake, std::memory_order_relaxed);
    }

    void wake() noexcept {
        if (state_.load(std::memory_order_relaxed) != kSleeping) return;
        if (state_.exchange(kAwake, std::memory_order_seq_cst) != kSleeping) return;
#if defined(__linux__)
        (void)::eventfd_write(efd_, 1);
#else
        std::lock_guard lk(mu_);
        cv_.notify_one();
#endif
    }

private:
    static constexpr u32 kAwake = 0;
    static constexpr u32 kSleeping = 1;

    alignas(kCacheLine) std::atomic<u32> state_{kAwake};
    int efd_ = -1;
#if !defined(__linux__)
    std::mutex mu_;
    std::condition_variable cv_;
#endif
};

}  // namespace sb
