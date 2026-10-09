#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>

#include "reboot/foundation/types.hpp"

namespace rb {

// Self-contained so reboot_client and winhost hash without OpenSSL.
class Sha256 {
public:
    Sha256() noexcept;

    void update(std::span<const u8> data) noexcept;
    [[nodiscard]] std::array<u8, 32> finish() noexcept;

private:
    void compress(const u8* block) noexcept;

    std::array<u32, 8> state_{};
    std::array<u8, 64> block_{};
    std::size_t block_used_ = 0;
    u64 total_bytes_ = 0;
};

[[nodiscard]] std::array<u8, 32> sha256(std::span<const u8> data) noexcept;

// Lowercase.
[[nodiscard]] std::string to_hex(std::span<const u8> data);

// Runs in time that depends only on the lengths.
[[nodiscard]] bool constant_time_equal(std::span<const u8> a, std::span<const u8> b) noexcept;

}  // namespace rb
