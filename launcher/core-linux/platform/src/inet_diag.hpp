#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "socket_table.hpp"

namespace rb::os_linux::platform {

enum class DiagProtocol : u8 { Tcp, Udp };

// Every socket of `protocol` in both families over NETLINK_SOCK_DIAG; nullopt where netlink or
// the protocol's diag module is refused, so the caller reads /proc/net instead.
[[nodiscard]] std::optional<std::vector<SocketRecord>> diag_dump(DiagProtocol protocol);

struct DiagLookup {
    // False where netlink is refused.
    bool answered = false;
    std::optional<SocketRecord> socket;
};

// The TCP socket whose own local end is `local` and remote end `remote`, by exact-tuple lookup.
[[nodiscard]] DiagLookup diag_find_tcp(Endpoint local, Endpoint remote);

}  // namespace rb::os_linux::platform
