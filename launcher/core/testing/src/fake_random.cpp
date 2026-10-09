#include "reboot/testing/fake_random.hpp"

#include <cstddef>
#include <mutex>
#include <span>

namespace rb::testing {

void FakeRandom::fill(std::span<u8> out) {
    const std::scoped_lock lock(mutex_);
    std::size_t at = 0;
    while (at < out.size()) {
        // SplitMix64.
        state_ += 0x9E3779B97F4A7C15ULL;
        u64 z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        // A zero word would make an all-zero token possible.
        if (z == 0) z = 1;
        for (int byte = 0; byte < 8 && at < out.size(); ++byte, ++at) out[at] = static_cast<u8>(z >> (8 * byte));
    }
}

}  // namespace rb::testing
