#pragma once

#include <chrono>
#include <optional>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class IClock;
}

namespace rb::browser {

struct TokenRate {
    u32 tokens = 1;
    std::chrono::milliseconds per{1000};
    u32 burst = 1;
};

// The edge's defaults: query_per_sec 10, query_burst 20; join_per_min 5, join_burst 5 per entry.
inline constexpr TokenRate kQueryRate{10, std::chrono::seconds{1}, 20};
inline constexpr TokenRate kJoinRate{5, std::chrono::minutes{1}, 5};

// Strand-only. Mirrors one of the edge's limiters so a well-behaved client rarely sees RATE_LIMITED.
class TokenBucket {
public:
    TokenBucket(const IClock& clock, TokenRate rate);

    // Takes a token, or returns how long until one is available.
    [[nodiscard]] std::optional<std::chrono::milliseconds> try_take();
    // After RATE_LIMITED: no token until `retry_after` has passed.
    void hold_off(std::chrono::milliseconds retry_after);

private:
    void refill();

    const IClock& clock_;
    TokenRate rate_;
    double tokens_ = 0;
    SteadyTime refilled_at_{};
    SteadyTime held_until_{};
};

}  // namespace rb::browser
