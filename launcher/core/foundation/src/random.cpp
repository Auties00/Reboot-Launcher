#include "reboot/foundation/random.hpp"

#include <vector>

#include "reboot/foundation/sha256.hpp"

namespace reboot {

Uuid uuid_v4(IRandom& random) {
    Uuid uuid;
    random.fill(uuid.bytes);
    uuid.bytes[6] = static_cast<u8>((uuid.bytes[6] & 0x0F) | 0x40);
    uuid.bytes[8] = static_cast<u8>((uuid.bytes[8] & 0x3F) | 0x80);
    return uuid;
}

std::string random_token_hex(IRandom& random, std::size_t bytes) {
    std::vector<u8> buffer(bytes);
    random.fill(buffer);
    return to_hex(buffer);
}

}  // namespace reboot
