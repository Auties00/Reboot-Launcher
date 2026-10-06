#pragma once

#include "core/mpsc.hpp"
#include "core/waker.hpp"

namespace sb {

// MPSC inbox bound to its consumer's waker (which may also cover other event sources).
class Mailbox {
public:
    explicit Mailbox(Waker& waker) noexcept : waker_(waker) {}

    void post(MpscNode* n) noexcept {
        queue_.push(n);
        waker_.wake();
    }
    [[nodiscard]] MpscNode* pop() noexcept { return queue_.pop(); }
    [[nodiscard]] bool empty() const noexcept { return queue_.empty(); }

private:
    MpscQueue queue_;
    Waker& waker_;
};

}  // namespace sb
