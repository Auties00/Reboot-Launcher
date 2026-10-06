#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "core/features.hpp"
#include "core/types.hpp"
#include "core/waker.hpp"

namespace sb {

// Single-producer broadcast ring (Disruptor style): every consumer sees every slot in order,
// each at its own cursor. One write per event regardless of consumer count. Consumer cursors
// double as quiescent-state counters: once min_cursor() > s, no consumer still reads slot s or
// anything that slot s announced as retired.
template <class T>
class BroadcastRing {
public:
    struct Consumer {
        alignas(kCacheLine) std::atomic<u64> cursor{0};
        Waker* waker = nullptr;
        std::atomic<bool> active{true};  // inactive consumers no longer hold back reclamation
    };

    explicit BroadcastRing(std::size_t capacity_pow2) : mask_(capacity_pow2 - 1), slots_(capacity_pow2) {
        SB_ASSERT((capacity_pow2 & mask_) == 0);
    }

    // Registration happens before the producer starts.
    Consumer& add_consumer(Waker* waker) {
        consumers_.push_back(std::make_unique<Consumer>());
        consumers_.back()->waker = waker;
        return *consumers_.back();
    }

    // Producer side. Blocks (spin, then yield) only if the slowest consumer is a full ring behind.
    template <class OnStall>
    u64 publish(T value, OnStall&& on_stall) {
        const u64 seq = next_;
        if (seq - cached_min_ >= slots_.size()) {
            cached_min_ = min_cursor();
            u32 spins = 0;
            while (seq - cached_min_ >= slots_.size()) {
                if (spins++ == 0) on_stall();
                wake_all();
                if (spins > 64) std::this_thread::yield();
                cached_min_ = min_cursor();
            }
        }
        slots_[seq & mask_] = std::move(value);
        next_ = seq + 1;
        published_.store(next_, std::memory_order_release);
        return seq;
    }

    u64 publish(T value) {
        return publish(std::move(value), [] {});
    }

    // Wakes consumers that announced sleep. Call once per batch of publishes.
    void wake_all() noexcept {
        for (auto& c : consumers_)
            if (c->waker) c->waker->wake();
    }

    [[nodiscard]] u64 min_cursor() const noexcept {
        u64 m = next_;
        for (const auto& c : consumers_) {
            if (!c->active.load(std::memory_order_acquire)) continue;
            const u64 v = c->cursor.load(std::memory_order_acquire);
            if (v < m) m = v;
        }
        return m;
    }

    [[nodiscard]] u64 next_seq() const noexcept { return next_; }

    // Consumer side.
    [[nodiscard]] u64 available() const noexcept { return published_.load(std::memory_order_acquire); }
    [[nodiscard]] const T& at(u64 seq) const noexcept { return slots_[seq & mask_]; }
    static void advance(Consumer& c, u64 to) noexcept { c.cursor.store(to, std::memory_order_release); }

private:
    const std::size_t mask_;
    std::vector<T> slots_;
    std::vector<std::unique_ptr<Consumer>> consumers_;
    alignas(kCacheLine) std::atomic<u64> published_{0};
    alignas(kCacheLine) u64 next_ = 0;
    u64 cached_min_ = 0;
};

}  // namespace sb
