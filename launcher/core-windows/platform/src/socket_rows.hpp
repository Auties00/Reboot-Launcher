#pragma once

#include <optional>
#include <span>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_windows::platform {

// One row of the IP Helper owner tables, v4 and v6 alike. TIME_WAIT rows carry pid 0.
struct SocketRow {
    IpAddress address;
    Port port;
    u32 pid = 0;
    bool listening = false;
};

// A listener on `local`'s port whose address is `local`'s or a wildcard covering it (0.0.0.0 for
// v4, :: for both, as a dual-stack socket takes v4 too), else a non-listening socket bound to
// exactly `local`; the exact address wins a tie.
[[nodiscard]] std::optional<u32> tcp_owner_pid(std::span<const SocketRow> rows, Endpoint local);
[[nodiscard]] std::optional<u32> udp_owner_pid(std::span<const SocketRow> rows, Port port);

}  // namespace reboot::os_windows::platform
