#pragma once

#include <chrono>

#include "reboot/foundation/function.hpp"
#include "reboot/testing/conformance_waiter.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// Polls on the real clock, for the real adapters in OS conformance and contract runs.
class WallClockWaiter final : public IConformanceWaiter {
public:
    explicit WallClockWaiter(std::chrono::milliseconds poll = std::chrono::milliseconds{5}) : poll_(poll) {}

    bool wait_until(UniqueFunction<bool()> condition, std::chrono::milliseconds budget) override;

private:
    std::chrono::milliseconds poll_;
};

}  // namespace reboot::testing
