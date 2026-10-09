#include "reboot/front/session_key.hpp"

#include "reboot/foundation/random.hpp"
#include "reboot/foundation/sha256.hpp"

namespace rb::front {

namespace {

[[nodiscard]] std::optional<u8> hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
    return std::nullopt;
}

}  // namespace

SessionKey SessionKey::generate(IRandom& random) {
    SessionKey key;
    random.fill(key.bytes);
    return key;
}

std::optional<SessionKey> SessionKey::parse(std::string_view hex) {
    SessionKey key;
    if (hex.size() != key.bytes.size() * 2) return std::nullopt;
    for (std::size_t i = 0; i < key.bytes.size(); ++i) {
        const auto high = hex_value(hex[2 * i]);
        const auto low = hex_value(hex[2 * i + 1]);
        if (!high || !low) return std::nullopt;
        key.bytes[i] = static_cast<u8>(*high << 4 | *low);
    }
    return key;
}

std::string SessionKey::to_hex() const { return rb::to_hex(bytes); }

}  // namespace rb::front
