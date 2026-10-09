#include "reboot/testing/deterministic_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace rb::testing {

std::size_t DeterministicRuntime::advance(std::chrono::steady_clock::duration by,
                                          std::chrono::steady_clock::duration step) {
    std::size_t ran = strand_.run_all();
    while (by > std::chrono::steady_clock::duration::zero()) {
        const auto slice = std::min(by, step);
        ran += strand_.advance(slice);
        ran += strand_.run_all();
        by -= slice;
    }
    return ran;
}

}  // namespace rb::testing
