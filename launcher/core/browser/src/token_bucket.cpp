#include "reboot/browser/token_bucket.hpp"

#include <algorithm>
#include <cmath>

#include "reboot/foundation/clock.hpp"

namespace reboot::browser {

namespace {

[[nodiscard]] std::chrono::milliseconds ceil_ms(std::chrono::duration<double, std::milli> wait) {
    return std::max(std::chrono::milliseconds{1},
                    std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(std::ceil(wait.count()))});
}

}  // namespace

TokenBucket::TokenBucket(const IClock& clock, TokenRate rate)
    : clock_(clock), rate_(rate), tokens_(rate.burst), refilled_at_(clock.steady_now()) {}

void TokenBucket::refill() {
    const SteadyTime now = clock_.steady_now();
    if (now <= refilled_at_) return;
    const std::chrono::duration<double, std::milli> elapsed = now - refilled_at_;
    const double per_ms = static_cast<double>(rate_.tokens) / static_cast<double>(rate_.per.count());
    tokens_ = std::min(static_cast<double>(rate_.burst), tokens_ + (elapsed.count() * per_ms));
    refilled_at_ = now;
}

std::optional<std::chrono::milliseconds> TokenBucket::try_take() {
    const SteadyTime now = clock_.steady_now();
    if (now < held_until_) return ceil_ms(held_until_ - now);
    refill();
    if (tokens_ >= 1.0) {
        tokens_ -= 1.0;
        return std::nullopt;
    }
    const double per_ms = static_cast<double>(rate_.tokens) / static_cast<double>(rate_.per.count());
    return ceil_ms(std::chrono::duration<double, std::milli>((1.0 - tokens_) / per_ms));
}

void TokenBucket::hold_off(std::chrono::milliseconds retry_after) {
    const SteadyTime until = clock_.steady_now() + retry_after;
    if (until <= held_until_) return;
    held_until_ = until;
    // The edge's hint is when its next token is due, so one is ready as the hold ends.
    tokens_ = 1;
    refilled_at_ = held_until_;
}

}  // namespace reboot::browser
