#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::platform {

// TCP_LISTEN and TCP_TIME_WAIT as the kernel numbers TCP states.
inline constexpr u8 kTcpListen = 10;
inline constexpr u8 kTcpTimeWait = 6;

// One socket from inet_diag or /proc/net/{tcp,tcp6,udp,udp6}.
struct SocketRecord {
    Endpoint local;
    Endpoint remote;
    u8 state = 0;
    u32 uid = 0;
    // 0 for sockets with no file, such as TIME_WAIT ones.
    u64 inode = 0;

    bool operator==(const SocketRecord&) const = default;
};

// The rows of one /proc/net table; `v6` for tcp6 and udp6. Addresses there are the kernel's
// network-order words printed as host-order hex, which the parser undoes for this host.
[[nodiscard]] std::vector<SocketRecord> parse_proc_net(std::string_view text, bool v6);

// 0.0.0.0 and ::, which bind every local address.
[[nodiscard]] bool is_wildcard(const IpAddress& address) noexcept;

// The socket bound to `local`, if any: an exact address, or a wildcard on its port (a v6 one
// covers v4 too, being dual-stack by default). A listener wins over the connections it accepted.
[[nodiscard]] std::optional<SocketRecord> find_tcp_owner(const std::vector<SocketRecord>& sockets, Endpoint local);

// The first socket with a file bound to `port` on any local address.
[[nodiscard]] std::optional<SocketRecord> find_udp_owner(const std::vector<SocketRecord>& sockets, Port port);

// The socket whose local end is `local` and remote end `remote`.
[[nodiscard]] std::optional<SocketRecord> find_connection(const std::vector<SocketRecord>& sockets, Endpoint local,
                                                          Endpoint remote);

}  // namespace reboot::os_linux::platform
