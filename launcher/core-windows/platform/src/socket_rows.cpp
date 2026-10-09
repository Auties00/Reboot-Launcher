#include "socket_rows.hpp"

namespace reboot::os_windows::platform {

namespace {

[[nodiscard]] bool covers(const IpAddress& bound, const IpAddress& local) noexcept {
    if (bound == local) return true;
    if (bound == IpAddress{}) return true;
    return bound == IpAddress::v4(0) && local.is_v4();
}

}  // namespace

std::optional<u32> tcp_owner_pid(std::span<const SocketRow> rows, Endpoint local) {
    std::optional<u32> wildcard;
    std::optional<u32> bound;
    for (const SocketRow& row : rows) {
        if (row.pid == 0 || row.port != local.port) continue;
        if (row.listening && row.address == local.address) return row.pid;
        if (row.listening && covers(row.address, local.address)) {
            if (!wildcard) wildcard = row.pid;
        } else if (!row.listening && row.address == local.address && !bound) {
            bound = row.pid;
        }
    }
    return wildcard ? wildcard : bound;
}

std::optional<u32> udp_owner_pid(std::span<const SocketRow> rows, Port port) {
    for (const SocketRow& row : rows)
        if (row.pid != 0 && row.port == port) return row.pid;
    return std::nullopt;
}

}  // namespace reboot::os_windows::platform
