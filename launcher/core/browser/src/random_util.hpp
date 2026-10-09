#pragma once

#include <chrono>
#include <cstring>

#include "reboot/foundation/random.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::browser {

// Uniform in [0, ceiling]; the modulo bias over 64 random bits is far below a millisecond.
[[nodiscard]] inline std::chrono::milliseconds uniform_delay(IRandom& random, std::chrono::milliseconds ceiling) {
    if (ceiling.count() <= 0) return std::chrono::milliseconds{0};
    const auto bytes = random_bytes<8>(random);
    u64 value = 0;
    std::memcpy(&value, bytes.data(), sizeof value);
    return std::chrono::milliseconds{
        static_cast<std::chrono::milliseconds::rep>(value % (static_cast<u64>(ceiling.count()) + 1))};
}

}  // namespace reboot::browser
