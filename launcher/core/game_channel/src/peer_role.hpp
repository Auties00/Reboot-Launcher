#pragma once

#include <string_view>

#include "reboot/contracts/game_client.hpp"
#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::game_channel {

// The {role} argument of this package's messages.
[[nodiscard]] constexpr std::string_view role_name(contracts::game_client::PeerRole role) noexcept {
    return role == contracts::game_client::PeerRole::Winhost ? "winhost" : "client_dll";
}

// Both peers ship with the engine, so the Hello's protocol must match exactly.
[[nodiscard]] constexpr u32 expected_protocol(contracts::game_client::PeerRole role) noexcept {
    return role == contracts::game_client::PeerRole::Winhost ? contracts::winhost::kWinhostProtocol
                                                              : u32{contracts::game_client::kPayloadAbi};
}

}  // namespace rb::game_channel
