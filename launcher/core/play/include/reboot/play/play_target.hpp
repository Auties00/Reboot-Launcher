#pragma once

#include <variant>

#include "reboot/browser/join_target.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::play {

// A server from the browser, joined through JoinService with a ConfirmJoin.
struct BrowserServerTarget {
    ServerId server;

    bool operator==(const BrowserServerTarget&) const = default;
};

// The linked auto-server: AutoServerConsent, then an Unlisted host session of the built-in auto
// profile, the play session's child.
struct AutoServerTarget {
    bool operator==(const AutoServerTarget&) const = default;
};

// Covers game-launch.orchestration.
// An address is parsed by browser::parse_game_server_address, then resolved and probed at start.
using PlayTarget = std::variant<BrowserServerTarget, browser::AddressTarget, AutoServerTarget>;

}  // namespace rb::play
