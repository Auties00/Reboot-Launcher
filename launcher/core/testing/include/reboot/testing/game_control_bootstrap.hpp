#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/process.hpp"

namespace rb::testing {

// What a game-control peer reads from its environment before it connects: REBOOT_CTL,
// REBOOT_CTL_TOKEN (43 unpadded base64url characters as the engine issues it, or 64 hex digits),
// REBOOT_SESSION and REBOOT_ROLE.
struct GameControlBootstrap {
    Endpoint engine;
    std::array<u8, 32> token{};
    SessionId session;
    std::string role;
};

// testing.bad_bootstrap names the first missing or malformed variable.
[[nodiscard]] Result<GameControlBootstrap> read_game_control_bootstrap(const ports::EnvBlock& env);
// From a SpawnGame env_block_utf16, which is how winhost hands the bootstrap to the game.
[[nodiscard]] Result<GameControlBootstrap> read_game_control_bootstrap(std::span<const u8> env_block_utf16);
// From this process's own environment, as reboot-fake-game reads it.
[[nodiscard]] Result<GameControlBootstrap> read_own_game_control_bootstrap();

}  // namespace rb::testing
