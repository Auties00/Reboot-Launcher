#pragma once

#include <variant>
#include <vector>

#include "reboot/gameserver/operator_command.hpp"
#include "reboot/host/host_ban.hpp"
#include "reboot/host/ip_cidr.hpp"

namespace rb::host {

// Stored in the session's profile first, so the next start keeps them, then sent to the server.
struct ReplaceBans {
    std::vector<HostBan> bans;
};
struct ReplaceOperators {
    std::vector<IpCidr> operator_cidrs;
};

// What an operator may send to a live session. Drain and Shutdown stay engine-only. Kick names the
// server's player id, since claimed account ids may collide.
using HostCommand = std::variant<gameserver::StartMatch, gameserver::EndMatch, gameserver::ResetMatch, gameserver::Kick,
                                 ReplaceBans, ReplaceOperators, gameserver::RunCommand>;

}  // namespace rb::host
