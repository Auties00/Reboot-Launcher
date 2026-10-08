#pragma once

#include <compare>
#include <string>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::game_channel {

// What a token admits; at most one live token and one connection per key.
// `module` is the peer's file name (rb_client.dll, reboot-winhost.exe).
struct PeerKey {
    SessionId session;
    contracts::game_client::PeerRole role{};
    std::string module;

    auto operator<=>(const PeerKey&) const = default;
};

}  // namespace reboot::game_channel
