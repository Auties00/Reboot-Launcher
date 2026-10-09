#pragma once

#include <mutex>
#include <span>

#include "reboot/foundation/random.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// A SplitMix64 stream from `seed`, so session keys, tokens and ids repeat from run to run. Never
// all zeros, so code that rejects a zero token still accepts it.
class FakeRandom final : public IRandom {
public:
    explicit FakeRandom(u64 seed = 1) : state_(seed) {}

    void fill(std::span<u8> out) override;

private:
    std::mutex mutex_;
    u64 state_;
};

}  // namespace rb::testing
