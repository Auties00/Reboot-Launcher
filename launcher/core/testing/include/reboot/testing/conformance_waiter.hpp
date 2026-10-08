#pragma once

#include <chrono>

#include "reboot/foundation/function.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// How a conformance suite waits for a port's callbacks. The condition runs on the waiting thread,
// so suites keep callback results behind a mutex.
class IConformanceWaiter {
public:
    virtual ~IConformanceWaiter() = default;

    // True once `condition` holds; false when `budget` ran out first.
    virtual bool wait_until(UniqueFunction<bool()> condition, std::chrono::milliseconds budget) = 0;
};

}  // namespace reboot::testing
