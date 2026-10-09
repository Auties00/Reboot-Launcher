#pragma once

#include <optional>
#include <span>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::os_macos::platform {

// One socket of PROC_PIDFDSOCKETINFO, reduced to its local side.
struct LocalSocket {
    bool tcp = false;
    // insi_vflag: INI_IPV4 and INI_IPV6. A dual-stack v6 socket carries both.
    bool v4 = false;
    bool v6 = false;
    // IPv4 is stored IPv4-mapped, as IpAddress does.
    IpAddress address;
    Port port;
};

// Bound to `local` itself, or to the wildcard of a family that covers it.
[[nodiscard]] bool owns_tcp(const LocalSocket& socket, Endpoint local) noexcept;
[[nodiscard]] bool owns_udp(const LocalSocket& socket, Port port) noexcept;

struct Candidate {
    u32 pid = 0;
    std::optional<NativePath> exe;
};

// Wine sockets are also held by wineserver: another holder is the owner, and wine_server marks either.
[[nodiscard]] std::optional<ports::PortOwner> choose_owner(std::span<const Candidate> holders);

}  // namespace reboot::os_macos::platform
