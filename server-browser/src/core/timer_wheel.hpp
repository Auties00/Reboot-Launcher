#pragma once

#include <vector>

#include "core/features.hpp"
#include "core/types.hpp"

namespace sb {

// Intrusive timer node; embed it in the object that owns the timer.
struct TimerNode {
    TimerNode* prev = nullptr;
    TimerNode* next = nullptr;
    u64 deadline_tick = 0;
    u64 cookie = 0;

    [[nodiscard]] bool armed() const noexcept { return prev != nullptr; }
};

// Hashed timing wheel, single-threaded: O(1) schedule/cancel/reschedule, which keeps
// heartbeat refreshes (one per host every few seconds) off any allocator or tree.
class TimerWheel {
public:
    explicit TimerWheel(u64 tick_ms = 100, std::size_t slots_pow2 = 4096)
        : tick_ms_(tick_ms), mask_(slots_pow2 - 1), slots_(slots_pow2) {
        SB_ASSERT((slots_pow2 & mask_) == 0);
        for (auto& s : slots_) s.prev = s.next = &s;
    }
    TimerWheel(const TimerWheel&) = delete;
    TimerWheel& operator=(const TimerWheel&) = delete;

    void start(u64 now_ms) noexcept { current_tick_ = now_ms / tick_ms_; }

    void schedule(TimerNode* n, u64 deadline_ms) noexcept {
        if (n->armed()) unlink(n);
        u64 tick = (deadline_ms + tick_ms_ - 1) / tick_ms_;
        if (tick <= current_tick_) tick = current_tick_ + 1;
        n->deadline_tick = tick;
        TimerNode& head = slots_[tick & mask_];
        n->next = &head;
        n->prev = head.prev;
        head.prev->next = n;
        head.prev = n;
        ++size_;
    }

    void cancel(TimerNode* n) noexcept {
        if (n->armed()) unlink(n);
    }

    // Fires every timer whose deadline passed. The callback may reschedule or cancel any node.
    template <class F>
    void advance(u64 now_ms, F&& fire) {
        const u64 target = now_ms / tick_ms_;
        while (current_tick_ < target) {
            ++current_tick_;
            TimerNode& head = slots_[current_tick_ & mask_];
            // Detach due nodes first so callbacks can safely touch the wheel.
            expired_.clear();
            for (TimerNode* n = head.next; n != &head;) {
                TimerNode* nx = n->next;
                if (n->deadline_tick <= current_tick_) {
                    unlink(n);
                    expired_.push_back(n);
                }
                n = nx;
            }
            for (TimerNode* n : expired_) fire(n);
        }
    }

    // Milliseconds until the next tick boundary, for poll timeouts.
    [[nodiscard]] u64 ms_to_next_tick(u64 now_ms) const noexcept {
        if (size_ == 0) return 1000;
        const u64 next = (now_ms / tick_ms_ + 1) * tick_ms_;
        return next - now_ms;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    void unlink(TimerNode* n) noexcept {
        n->prev->next = n->next;
        n->next->prev = n->prev;
        n->prev = n->next = nullptr;
        --size_;
    }

    const u64 tick_ms_;
    const std::size_t mask_;
    std::vector<TimerNode> slots_;
    std::vector<TimerNode*> expired_;
    u64 current_tick_ = 0;
    std::size_t size_ = 0;
};

}  // namespace sb
