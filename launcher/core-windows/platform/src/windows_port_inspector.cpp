#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_port_inspector.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "socket_rows.hpp"
#include "unique_handle.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

[[nodiscard]] Port port_of(DWORD network_order) noexcept { return Port{ntohs(static_cast<u16>(network_order))}; }

[[nodiscard]] IpAddress v6_of(const UCHAR (&bytes)[16]) noexcept {
    IpAddress address;
    std::copy(std::begin(bytes), std::end(bytes), address.bytes.begin());
    return address;
}

// Grows the buffer until the table fits; tables change between calls, hence the loop.
template <class Fetch>
[[nodiscard]] Result<std::vector<u64>> fetch_table(std::string_view call, Fetch fetch) {
    DWORD size = 16 * 1024;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::vector<u64> buffer(size / sizeof(u64) + 1);
        DWORD capacity = static_cast<DWORD>(buffer.size() * sizeof(u64));
        const DWORD error = fetch(buffer.data(), &capacity);
        if (error == NO_ERROR) return buffer;
        if (error != ERROR_INSUFFICIENT_BUFFER) return std::unexpected(call_failed(call, error));
        size = capacity + 4096;
    }
    return std::unexpected(call_failed(call, ERROR_INSUFFICIENT_BUFFER));
}

[[nodiscard]] Result<std::vector<SocketRow>> tcp_rows() {
    std::vector<SocketRow> rows;
    auto v4 = fetch_table("GetExtendedTcpTable", [](void* table, DWORD* size) {
        return GetExtendedTcpTable(table, size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    });
    if (!v4) return std::unexpected(std::move(v4.error()));
    const auto* table4 = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(v4->data());
    for (DWORD i = 0; i < table4->dwNumEntries; ++i) {
        const MIB_TCPROW_OWNER_PID& row = table4->table[i];
        rows.push_back({IpAddress::v4(ntohl(row.dwLocalAddr)), port_of(row.dwLocalPort), row.dwOwningPid,
                        row.dwState == MIB_TCP_STATE_LISTEN});
    }
    auto v6 = fetch_table("GetExtendedTcpTable", [](void* table, DWORD* size) {
        return GetExtendedTcpTable(table, size, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    });
    if (!v6) return std::unexpected(std::move(v6.error()));
    const auto* table6 = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(v6->data());
    for (DWORD i = 0; i < table6->dwNumEntries; ++i) {
        const MIB_TCP6ROW_OWNER_PID& row = table6->table[i];
        rows.push_back({v6_of(row.ucLocalAddr), port_of(row.dwLocalPort), row.dwOwningPid, row.dwState == MIB_TCP_STATE_LISTEN});
    }
    return rows;
}

[[nodiscard]] Result<std::vector<SocketRow>> udp_rows() {
    std::vector<SocketRow> rows;
    auto v4 = fetch_table("GetExtendedUdpTable", [](void* table, DWORD* size) {
        return GetExtendedUdpTable(table, size, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    });
    if (!v4) return std::unexpected(std::move(v4.error()));
    const auto* table4 = reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(v4->data());
    for (DWORD i = 0; i < table4->dwNumEntries; ++i) {
        const MIB_UDPROW_OWNER_PID& row = table4->table[i];
        rows.push_back({IpAddress::v4(ntohl(row.dwLocalAddr)), port_of(row.dwLocalPort), row.dwOwningPid, false});
    }
    auto v6 = fetch_table("GetExtendedUdpTable", [](void* table, DWORD* size) {
        return GetExtendedUdpTable(table, size, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    });
    if (!v6) return std::unexpected(std::move(v6.error()));
    const auto* table6 = reinterpret_cast<const MIB_UDP6TABLE_OWNER_PID*>(v6->data());
    for (DWORD i = 0; i < table6->dwNumEntries; ++i) {
        const MIB_UDP6ROW_OWNER_PID& row = table6->table[i];
        rows.push_back({v6_of(row.ucLocalAddr), port_of(row.dwLocalPort), row.dwOwningPid, false});
    }
    return rows;
}

// System (pid 4, e.g. HTTP.sys on :80) and protected processes have no readable image path.
[[nodiscard]] ports::PortOwner owner_of(u32 pid) {
    ports::PortOwner owner;
    owner.pid = pid;
    UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process) return owner;
    std::vector<wchar_t> path(32 * 1024);
    DWORD length = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process.get(), 0, path.data(), &length) != 0)
        owner.exe = NativePath(std::wstring(path.data(), length));
    return owner;
}

}  // namespace

Result<std::optional<ports::PortOwner>> WindowsPortInspector::tcp_owner(Endpoint local) {
    auto rows = tcp_rows();
    if (!rows) return std::unexpected(std::move(rows.error()));
    const std::optional<u32> pid = tcp_owner_pid(*rows, local);
    if (!pid) return std::optional<ports::PortOwner>{};
    return std::optional<ports::PortOwner>{owner_of(*pid)};
}

Result<std::optional<ports::PortOwner>> WindowsPortInspector::udp_owner(Port port) {
    auto rows = udp_rows();
    if (!rows) return std::unexpected(std::move(rows.error()));
    const std::optional<u32> pid = udp_owner_pid(*rows, port);
    if (!pid) return std::optional<ports::PortOwner>{};
    return std::optional<ports::PortOwner>{owner_of(*pid)};
}

}  // namespace rb::os_windows::platform
