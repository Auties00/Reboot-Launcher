#pragma once

#include <chrono>

#include "core/types.hpp"

namespace sb {

[[nodiscard]] inline u64 mono_ns() noexcept {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

[[nodiscard]] inline u64 mono_us() noexcept { return mono_ns() / 1000; }
[[nodiscard]] inline u64 mono_ms() noexcept { return mono_ns() / 1'000'000; }

[[nodiscard]] inline u64 wall_ms() noexcept {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count());
}

}  // namespace sb
