#include "pem.hpp"

#include <cstddef>
#include <string_view>

namespace rb::os_macos::platform {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::size_t kLineLength = 64;

[[nodiscard]] std::string base64(std::span<const u8> bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const u32 group = (u32{bytes[i]} << 16) | (u32{bytes[i + 1]} << 8) | u32{bytes[i + 2]};
        out += kAlphabet[(group >> 18) & 0x3F];
        out += kAlphabet[(group >> 12) & 0x3F];
        out += kAlphabet[(group >> 6) & 0x3F];
        out += kAlphabet[group & 0x3F];
    }
    const std::size_t rest = bytes.size() - i;
    if (rest > 0) {
        u32 group = u32{bytes[i]} << 16;
        if (rest == 2) group |= u32{bytes[i + 1]} << 8;
        out += kAlphabet[(group >> 18) & 0x3F];
        out += kAlphabet[(group >> 12) & 0x3F];
        out += rest == 2 ? kAlphabet[(group >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

}  // namespace

std::string pem_bundle(std::span<const std::vector<u8>> certificates) {
    std::string out;
    for (const std::vector<u8>& der : certificates) {
        const std::string encoded = base64(der);
        out += "-----BEGIN CERTIFICATE-----\n";
        for (std::size_t line = 0; line < encoded.size(); line += kLineLength) {
            out.append(encoded, line, kLineLength);
            out += '\n';
        }
        out += "-----END CERTIFICATE-----\n";
    }
    return out;
}

}  // namespace rb::os_macos::platform
