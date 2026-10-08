#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::game_channel {

inline constexpr std::size_t kControlTokenSize = 32;

// Covers no capability ids (decision game-control-channel).
// The 32 CSPRNG bytes that admit one peer to the game channel. Move-only and wiped on destruction;
// only TokenRegistry mints one.
class ControlToken {
public:
    ControlToken(ControlToken&&) noexcept = default;
    ControlToken& operator=(ControlToken&&) noexcept = default;

    // The REBOOT_CTL_TOKEN value: unpadded base64url, 43 characters.
    [[nodiscard]] SecretString env_value() const;

    // Constant time in the contents of both sides.
    [[nodiscard]] bool matches(std::span<const u8, kControlTokenSize> candidate) const noexcept;

private:
    friend class TokenRegistry;
    explicit ControlToken(const std::array<u8, kControlTokenSize>& bytes) noexcept : bytes_(bytes) {}

    Secret<std::array<u8, kControlTokenSize>> bytes_;
};

}  // namespace reboot::game_channel
