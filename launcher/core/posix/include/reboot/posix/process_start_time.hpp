#pragma once

#include <chrono>

namespace rb::posix {

// Linux derives start times from /proc/stat btime, which is whole seconds and moves when the
// wall clock is stepped, so a recorded start time matches within this much.
inline constexpr std::chrono::seconds kStartTimeTolerance{2};

// The pid-reuse guard of every POSIX is_alive and kill.
[[nodiscard]] constexpr bool same_start_time(std::chrono::system_clock::time_point recorded,
                                             std::chrono::system_clock::time_point read) noexcept {
    return recorded - read <= kStartTimeTolerance && read - recorded <= kStartTimeTolerance;
}

}  // namespace rb::posix
