#include "socket_match.hpp"

namespace reboot::os_macos::platform {

namespace {

[[nodiscard]] constexpr bool is_unspecified_v6(const IpAddress& address) noexcept {
    for (const u8 byte : address.bytes)
        if (byte != 0) return false;
    return true;
}

// 0.0.0.0, stored mapped, or a v4 field the kernel left all zero.
[[nodiscard]] constexpr bool is_unspecified_v4(const IpAddress& address) noexcept {
    return is_unspecified_v6(address) || address == IpAddress::v4(0);
}

[[nodiscard]] bool is_wineserver(const std::optional<NativePath>& exe) {
    return exe && exe->filename() == "wineserver";
}

}  // namespace

bool owns_tcp(const LocalSocket& socket, Endpoint local) noexcept {
    if (!socket.tcp || socket.port != local.port) return false;
    if (local.address.is_v4()) {
        if (!socket.v4) return false;
        if (socket.v6) return is_unspecified_v6(socket.address) || socket.address == local.address;
        return is_unspecified_v4(socket.address) || socket.address == local.address;
    }
    if (!socket.v6) return false;
    return is_unspecified_v6(socket.address) || socket.address == local.address;
}

bool owns_udp(const LocalSocket& socket, Port port) noexcept { return !socket.tcp && socket.port == port; }

std::optional<ports::PortOwner> choose_owner(std::span<const Candidate> holders) {
    const Candidate* wine_server = nullptr;
    const Candidate* process = nullptr;
    for (const Candidate& holder : holders) {
        const Candidate*& slot = is_wineserver(holder.exe) ? wine_server : process;
        if (slot == nullptr) slot = &holder;
    }
    const Candidate* owner = process != nullptr ? process : wine_server;
    if (owner == nullptr) return std::nullopt;
    return ports::PortOwner{.pid = owner->pid, .exe = owner->exe, .wine_server = wine_server != nullptr};
}

}  // namespace reboot::os_macos::platform
