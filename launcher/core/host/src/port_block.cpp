#include "reboot/host/port_block.hpp"

namespace rb::host {

std::vector<Port> PortBlock::ports() const {
    std::vector<Port> out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) out.push_back(at(i));
    return out;
}

Port game_port(const PortBlock& block, const std::vector<gameserver::SocketRole>& roles) {
    for (std::size_t i = 0; i < roles.size() && i < block.size; ++i)
        if (roles[i] == gameserver::SocketRole::Game) return block.at(i);
    return block.first;
}

}  // namespace rb::host
