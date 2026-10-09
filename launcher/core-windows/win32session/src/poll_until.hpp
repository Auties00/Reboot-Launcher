#pragma once

#include <algorithm>
#include <chrono>

namespace reboot::os_windows::win32session {

// Tries `probe` until it holds or `bound` has passed, calling `pause(wait)` between tries with
// `wait` clamped to the time left; `pause` may return early. A zero bound probes exactly once.
// `now` returns a steady time point, so tests drive the loop without sleeping.
template <class Probe, class Pause, class Now>
[[nodiscard]] bool poll_until(Probe&& probe, Pause&& pause, Now&& now, std::chrono::milliseconds bound,
                              std::chrono::milliseconds step) {
    const auto deadline = now() + bound;
    for (;;) {
        if (probe()) return true;
        const auto current = now();
        if (current >= deadline) return false;
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - current);
        pause(std::max(std::chrono::milliseconds{1}, std::min(step, left)));
    }
}

}  // namespace reboot::os_windows::win32session
