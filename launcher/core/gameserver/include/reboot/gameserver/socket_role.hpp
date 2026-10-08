#pragma once

#include <vector>

#include "reboot/contracts/game_server.hpp"
#include "reboot/gameserver/game_server_description.hpp"

namespace reboot::gameserver {

// The Game socket answers the rbsb/1 probe and is the port that gets published.
using SocketRole = contracts::game_server::SocketRole;

// One entry per declared socket, in order: the port block has exactly this many ports and
// GameServerConfig::listen.ports[i] is bound for roles[i].
[[nodiscard]] std::vector<SocketRole> socket_roles(const GameServerDescription& description);

}  // namespace reboot::gameserver
