#include "reboot/testing/manual_waiter.hpp"

#include <algorithm>
#include <chrono>

#include "reboot/testing/deterministic_runtime.hpp"

namespace rb::testing {

bool ManualWaiter::wait_until(UniqueFunction<bool()> condition, std::chrono::milliseconds budget) {
    constexpr std::chrono::milliseconds kStep{10};
    runtime_.run_until_idle();
    while (!condition()) {
        if (budget <= std::chrono::milliseconds::zero()) return false;
        const auto slice = std::min(budget, kStep);
        runtime_.advance(slice, slice);
        budget -= slice;
    }
    return true;
}

}  // namespace rb::testing
