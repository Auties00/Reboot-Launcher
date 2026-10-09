#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>

#include "reboot/foundation/types.hpp"

namespace rb {

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual void fill(std::span<u8> out) = 0;
};

// BCryptGenRandom on Windows, getentropy/arc4random_buf on POSIX. A CSPRNG failure aborts:
// no caller can recover from it safely.
class OsRandom final : public IRandom {
public:
    void fill(std::span<u8> out) override;
};

template <std::size_t N>
[[nodiscard]] std::array<u8, N> random_bytes(IRandom& random) {
    std::array<u8, N> out{};
    random.fill(out);
    return out;
}

[[nodiscard]] Uuid uuid_v4(IRandom& random);

[[nodiscard]] std::string random_token_hex(IRandom& random, std::size_t bytes);

}  // namespace rb
