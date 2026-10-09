#include "reboot/browser/full_jitter_backoff.hpp"

#include <algorithm>
#include <limits>

#include "random_util.hpp"

namespace rb::browser {

namespace {

// kBase * 2^5 already exceeds kCap, so larger exponents change nothing.
constexpr u32 kMaxExponent = 5;

}  // namespace

std::chrono::milliseconds FullJitterBackoff::next() {
    const auto ceiling = std::min(kCap, kBase * (std::chrono::milliseconds::rep{1} << std::min(attempt_, kMaxExponent)));
    if (attempt_ < std::numeric_limits<u32>::max()) ++attempt_;
    return uniform_delay(random_, ceiling);
}

std::chrono::milliseconds go_away_delay(IRandom& random, std::chrono::milliseconds reconnect_after) {
    return uniform_delay(random, std::max(reconnect_after, std::chrono::milliseconds{0}));
}

}  // namespace rb::browser
