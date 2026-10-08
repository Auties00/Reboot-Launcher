#pragma once

#include <array>

#include "reboot/contracts/game_server.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::gameserver {

// The foundation has no named digest type; this matches components::Sha256Digest.
using Sha256Digest = std::array<u8, 32>;

using GameServerDescription = contracts::game_server::GameServerDescription;
using GameServerCapabilities = contracts::game_server::ServerCapabilities;

// A binary as it was described: what sizes the port block, feeds the SupportPolicy host cells
// and must match the ServerHello of every process spawned from it.
struct DescribedBinary {
    NativePath exe;
    Sha256Digest sha256{};
    GameServerDescription description;
};

// Compares the encoded forms, so every field counts, in order.
[[nodiscard]] bool same_description(const GameServerDescription& a, const GameServerDescription& b);

}  // namespace reboot::gameserver
