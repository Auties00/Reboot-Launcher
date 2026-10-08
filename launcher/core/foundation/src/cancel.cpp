#include "reboot/foundation/cancel.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace reboot {

namespace detail {

struct CancelState {
    static constexpr int kNotCancelled = -1;

    std::mutex mutex;
    // The reason is published here, by CAS, before any callback runs.
    std::atomic<int> reason{kNotCancelled};
    u64 next_id = 1;
    std::map<u64, UniqueFunction<void(CancelReason)>> callbacks;
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
        const std::lock_guard lock(state_->mutex);
        if (const auto it = state_->callbacks.find(id_); it != state_->callbacks.end()) {
            removed = std::move(it->second);
            state_->callbacks.erase(it);
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
    std::vector<UniqueFunction<void(CancelReason)>> callbacks;
    {
        const std::lock_guard lock(state_->mutex);
        int expected = detail::CancelState::kNotCancelled;
        if (!state_->reason.compare_exchange_strong(expected, static_cast<int>(reason), std::memory_order_acq_rel))
            return false;
        callbacks.reserve(state_->callbacks.size());
        for (auto& entry : state_->callbacks) callbacks.push_back(std::move(entry.second));
        state_->callbacks.clear();
    }
    for (auto& callback : callbacks) callback(reason);
    return true;
}

bool CancelSource::cancelled() const noexcept {
    return state_->reason.load(std::memory_order_acquire) != detail::CancelState::kNotCancelled;
}

}  // namespace reboot
