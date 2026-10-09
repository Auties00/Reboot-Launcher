#pragma once

#include <chrono>

#include "reboot/foundation/function.hpp"
#include "reboot/testing/conformance_waiter.hpp"

namespace rb::testing {

class DeterministicRuntime;

// Covers no capability ids (decision testing-strategy).
// Runs ready work and advances manual time, so a fake's budget costs no real time.
class ManualWaiter final : public IConformanceWaiter {
public:
    explicit ManualWaiter(DeterministicRuntime& runtime) : runtime_(runtime) {}

    bool wait_until(UniqueFunction<bool()> condition, std::chrono::milliseconds budget) override;

private:
    DeterministicRuntime& runtime_;
};

}  // namespace rb::testing
