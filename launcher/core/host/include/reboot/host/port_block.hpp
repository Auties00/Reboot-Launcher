#pragma once

#include <cstddef>
#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/gameserver/socket_role.hpp"

namespace rb::host {

// Contiguous ports, one per socket the game server declares, in declaration order.
// at() and last() require fits(); HostPortAllocator only hands out blocks that fit.
struct PortBlock {
    Port first;
    u16 size = 0;

    [[nodiscard]] constexpr bool fits() const noexcept { return size > 0 && first.value + size <= 65536u; }
    [[nodiscard]] constexpr Port at(std::size_t index) const noexcept {
        return Port{static_cast<u16>(first.value + index)};
    }
    [[nodiscard]] constexpr Port last() const noexcept { return at(size - 1u); }
    [[nodiscard]] constexpr bool contains(Port port) const noexcept {
        return port >= first && port.value < first.value + size;
    }
    [[nodiscard]] constexpr bool overlaps(const PortBlock& other) const noexcept {
        return first.value < other.first.value + other.size && other.first.value < first.value + size;
    }
    [[nodiscard]] std::vector<Port> ports() const;

    constexpr auto operator<=>(const PortBlock&) const = default;
};

// The port of the first Game socket: the one probed, mapped first and published. `roles` comes
// from gameserver::socket_roles, whose description check guarantees a Game socket.
[[nodiscard]] Port game_port(const PortBlock& block, const std::vector<gameserver::SocketRole>& roles);

}  // namespace rb::host
