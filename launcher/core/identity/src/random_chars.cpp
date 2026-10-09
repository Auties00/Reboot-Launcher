#include "random_chars.hpp"

#include <array>

#include "reboot/foundation/random.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::identity {

std::string random_chars(IRandom& random, std::string_view alphabet, std::size_t count) {
    // Bytes at or past the last whole multiple of the alphabet size are redrawn, so no char is favoured.
    const std::size_t limit = 256 - 256 % alphabet.size();
    std::string out;
    out.reserve(count);
    while (out.size() < count) {
        std::array<u8, 16> bytes{};
        random.fill(bytes);
        for (const u8 byte : bytes) {
            if (byte >= limit) continue;
            out.push_back(alphabet[byte % alphabet.size()]);
            if (out.size() == count) break;
        }
    }
    return out;
}

std::string keep_ascii_alnum(std::string_view text) {
    std::string out;
    for (const char c : text)
        if (is_ascii_alnum(c)) out.push_back(c);
    return out;
}

}  // namespace rb::identity
