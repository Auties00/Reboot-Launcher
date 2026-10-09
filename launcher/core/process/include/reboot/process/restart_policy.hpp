#pragma once

#include <chrono>

#include "reboot/foundation/types.hpp"

namespace rb::process {

// Covers no capability ids. The backend's reaction to a crash or hang: the delay doubles from
// first_delay up to max_delay with each restart still inside `window`, and once max_restarts
// restarts fall inside the window the next crash ends in Failed.
struct RestartPolicy {
    std::chrono::milliseconds first_delay = std::chrono::seconds{1};
    std::chrono::milliseconds max_delay = std::chrono::seconds{30};
    u32 max_restarts = 5;
    std::chrono::milliseconds window = std::chrono::minutes{5};

    [[nodiscard]] constexpr bool allows(u32 restarts_in_window) const noexcept {
        return restarts_in_window < max_restarts;
    }

    [[nodiscard]] constexpr std::chrono::milliseconds delay(u32 restarts_in_window) const noexcept {
        std::chrono::milliseconds next = first_delay;
        for (u32 i = 0; i < restarts_in_window && next < max_delay; ++i) next *= 2;
        return next < max_delay ? next : max_delay;
    }
};

}  // namespace rb::process
