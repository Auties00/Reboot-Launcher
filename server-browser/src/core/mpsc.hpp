#pragma once

#include <atomic>

#include "core/features.hpp"

namespace sb {

struct MpscNode {
    std::atomic<MpscNode*> next{nullptr};
};

// Vyukov intrusive multi-producer single-consumer queue: wait-free push, lock-free pop.
class MpscQueue {
public:
    MpscQueue() noexcept : head_(&stub_), tail_(&stub_) {}
    MpscQueue(const MpscQueue&) = delete;
    MpscQueue& operator=(const MpscQueue&) = delete;

    void push(MpscNode* n) noexcept {
        n->next.store(nullptr, std::memory_order_relaxed);
        MpscNode* prev = head_.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    // Returns nullptr when empty or when a producer is between exchange and link (retry later).
    [[nodiscard]] MpscNode* pop() noexcept {
        MpscNode* tail = tail_;
        MpscNode* next = tail->next.load(std::memory_order_acquire);
        if (tail == &stub_) {
            if (!next) return nullptr;
            tail_ = next;
            tail = next;
            next = next->next.load(std::memory_order_acquire);
        }
        if (next) {
            tail_ = next;
            return tail;
        }
        if (tail != head_.load(std::memory_order_acquire)) return nullptr;
        push(&stub_);
        next = tail->next.load(std::memory_order_acquire);
        if (next) {
            tail_ = next;
            return tail;
        }
        return nullptr;
    }

    [[nodiscard]] bool empty() const noexcept {
        return tail_ == &stub_ ? stub_.next.load(std::memory_order_acquire) == nullptr : false;
    }

private:
    alignas(kCacheLine) std::atomic<MpscNode*> head_;
    alignas(kCacheLine) MpscNode* tail_;
    MpscNode stub_;
};

}  // namespace sb
