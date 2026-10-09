#pragma once

#include <chrono>

#include "reboot/foundation/types.hpp"

namespace rb {
class IRandom;
}

namespace rb::browser {

// Delay n is uniform in [0, min(cap, base * 2^n)], so clients cut off together by an edge restart
// do not come back in step.
class FullJitterBackoff {
public:
    static constexpr std::chrono::milliseconds kBase = std::chrono::seconds{1};
    static constexpr std::chrono::milliseconds kCap = std::chrono::seconds{30};

    explicit FullJitterBackoff(IRandom& random) : random_(random) {}

    [[nodiscard]] std::chrono::milliseconds next();
    // After a Welcome.
    void reset() noexcept { attempt_ = 0; }
    [[nodiscard]] u32 attempt() const noexcept { return attempt_; }

private:
    IRandom& random_;
    u32 attempt_ = 0;
};

// GoAway: a uniform delay in [0, reconnect_after], so a draining edge's clients spread out.
[[nodiscard]] std::chrono::milliseconds go_away_delay(IRandom& random, std::chrono::milliseconds reconnect_after);

}  // namespace rb::browser
