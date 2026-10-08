#include "reboot/testing/wall_clock_waiter.hpp"

#include <chrono>
#include <thread>

namespace reboot::testing {

bool WallClockWaiter::wait_until(UniqueFunction<bool()> condition, std::chrono::milliseconds budget) {
    const auto give_up = std::chrono::steady_clock::now() + budget;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= give_up) return condition();
        std::this_thread::sleep_for(poll_);
    }
    return true;
}

}  // namespace reboot::testing
