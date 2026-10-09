#include "reboot/gameserver/socket_role.hpp"

namespace rb::gameserver {

std::vector<SocketRole> socket_roles(const GameServerDescription& description) {
    std::vector<SocketRole> roles;
    roles.reserve(description.sockets.size());
    for (const contracts::game_server::SocketSpec& socket : description.sockets) roles.push_back(socket.role);
    return roles;
}

}  // namespace rb::gameserver
