#pragma once

#include <variant>

#include "reboot/base/types.hpp"
#include "reboot/browser/join_service.hpp"
#include "reboot/browser/join_target.hpp"

namespace rb::play {

// A server from the browser, joined through JoinService with a ConfirmJoin unless the UI already asked.
struct BrowserServerTarget {
    ServerId server;
    browser::JoinConfirmation confirmation = browser::JoinConfirmation::Ask;

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
