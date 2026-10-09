#include "reboot/foundation/cancel.hpp"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace rb {

namespace detail {

struct CancelState {
    static constexpr int kNotCancelled = -1;

    std::mutex mutex;
    std::condition_variable callback_done;
    // The reason is published here, by CAS, before any callback runs.
    std::atomic<int> reason{kNotCancelled};
    u64 next_id = 1;
    std::map<u64, UniqueFunction<void(CancelReason)>> callbacks;
    // The registration whose callback the cancelling thread is running, 0 when none.
    u64 running = 0;
    std::thread::id canceller;
};

}  // namespace detail

CancelRegistration::CancelRegistration(std::shared_ptr<detail::CancelState> state, u64 id)
    : state_(std::move(state)), id_(id) {}

CancelRegistration::CancelRegistration(CancelRegistration&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

CancelRegistration& CancelRegistration::operator=(CancelRegistration&& other) noexcept {
    if (this != &other) {
        reset();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

CancelRegistration::~CancelRegistration() { reset(); }

void CancelRegistration::reset() {
    if (!state_) return;
    // Taken out under the lock and destroyed after it, in case the callback owns a registration.
    UniqueFunction<void(CancelReason)> removed;
    {
        std::unique_lock lock(state_->mutex);
        if (const auto it = state_->callbacks.find(id_); it != state_->callbacks.end()) {
            removed = std::move(it->second);
            state_->callbacks.erase(it);
        } else if (state_->running == id_ && state_->canceller != std::this_thread::get_id()) {
            // Once reset returns the callback has finished, so its captures may be destroyed.
            state_->callback_done.wait(lock, [&] { return state_->running != id_; });
        }
    }
    state_.reset();
    id_ = 0;
}

CancelToken::CancelToken(std::shared_ptr<detail::CancelState> state) : state_(std::move(state)) {}

bool CancelToken::cancelled() const noexcept {
    return state_ && state_->reason.load(std::memory_order_acquire) != detail::CancelState::kNotCancelled;
}

std::optional<CancelReason> CancelToken::reason() const noexcept {
    if (!state_) return std::nullopt;
    const int reason = state_->reason.load(std::memory_order_acquire);
    if (reason == detail::CancelState::kNotCancelled) return std::nullopt;
    return static_cast<CancelReason>(reason);
}

CancelRegistration CancelToken::on_cancel(UniqueFunction<void(CancelReason)> callback) const {
    if (!state_) return {};
    std::unique_lock lock(state_->mutex);
    const int reason = state_->reason.load(std::memory_order_acquire);
    if (reason != detail::CancelState::kNotCancelled) {
        lock.unlock();
        callback(static_cast<CancelReason>(reason));
        return {};
    }
    const u64 id = state_->next_id++;
    state_->callbacks.emplace(id, std::move(callback));
    return CancelRegistration(state_, id);
}

CancelSource::CancelSource() : state_(std::make_shared<detail::CancelState>()) {}

CancelToken CancelSource::token() const { return CancelToken(state_); }

bool CancelSource::cancel(CancelReason reason) {
    // A callback may destroy this source, so the loop holds the state itself.
    const std::shared_ptr<detail::CancelState> state = state_;
    std::unique_lock lock(state->mutex);
    int expected = detail::CancelState::kNotCancelled;
    if (!state->reason.compare_exchange_strong(expected, static_cast<int>(reason), std::memory_order_acq_rel))
        return false;
    state->canceller = std::this_thread::get_id();
    // One at a time, so a registration reset before its turn never runs.
    while (!state->callbacks.empty()) {
        const auto next = state->callbacks.begin();
        state->running = next->first;
        UniqueFunction<void(CancelReason)> callback = std::move(next->second);
        state->callbacks.erase(next);
        lock.unlock();
        callback(reason);
        callback = nullptr;
        lock.lock();
        state->running = 0;
        state->callback_done.notify_all();
    }
    return true;
}

bool CancelSource::cancelled() const noexcept {
    return state_->reason.load(std::memory_order_acquire) != detail::CancelState::kNotCancelled;
}

}  // namespace rb
