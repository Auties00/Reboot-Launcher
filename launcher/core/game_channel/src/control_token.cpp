#include "reboot/game_channel/control_token.hpp"

#include <string>
#include <string_view>

namespace reboot::game_channel {

namespace {

constexpr std::string_view kBase64Url = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

}  // namespace

SecretString ControlToken::env_value() const {
    const auto& bytes = bytes_.reveal();
    std::string text;
    text.reserve((bytes.size() * 8 + 5) / 6);
    u32 bits = 0;
    int held = 0;
    for (const u8 byte : bytes) {
        bits = bits << 8 | byte;
        held += 8;
        while (held >= 6) {
            held -= 6;
            text.push_back(kBase64Url[(bits >> held) & 0x3F]);
        }
    }
    if (held > 0) text.push_back(kBase64Url[(bits << (6 - held)) & 0x3F]);
    return SecretString(std::move(text));
}

bool ControlToken::matches(std::span<const u8, kControlTokenSize> candidate) const noexcept {
    const auto& bytes = bytes_.reveal();
    u8 difference = 0;
    for (std::size_t i = 0; i < kControlTokenSize; ++i) difference |= static_cast<u8>(bytes[i] ^ candidate[i]);
    return difference == 0;
}

}  // namespace reboot::game_channel
