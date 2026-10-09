#include "darwin.hpp"

#include "reboot/os_macos/platform/libproc_port_inspector.hpp"

#include <libproc.h>
#include <netinet/in.h>
#include <sys/proc_info.h>
#include <sys/socket.h>

#include <array>
#include <bit>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "reboot/foundation/function.hpp"
#include "reboot/posix/posix_error.hpp"
#include "socket_match.hpp"

namespace rb::os_macos::platform {

namespace {

// ntohs and ntohl are macros full of C-style casts.
template <class T>
[[nodiscard]] constexpr T from_network(T value) noexcept {
    if constexpr (std::endian::native == std::endian::little) return std::byteswap(value);
    return value;
}

[[nodiscard]] Result<std::vector<pid_t>> all_pids() {
    std::vector<pid_t> pids(1024);
    for (;;) {
        const int capacity = static_cast<int>(pids.size() * sizeof(pid_t));
        const int bytes = ::proc_listallpids(pids.data(), capacity);
        if (bytes < 0) return std::unexpected(posix::call_failed("proc_listallpids", errno));
        // proc_listallpids counts pids, not bytes; a full buffer may have cut the list short.
        const auto count = static_cast<std::size_t>(bytes);
        if (count < pids.size()) {
            pids.resize(count);
            return pids;
        }
        pids.resize(pids.size() * 2);
    }
}

// The process's descriptors; empty for a process of another user or one that just exited.
[[nodiscard]] std::vector<proc_fdinfo> descriptors_of(pid_t pid) {
    const int needed = ::proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (needed <= 0) return {};
    // Room for descriptors opened between the two calls.
    std::vector<proc_fdinfo> fds(static_cast<std::size_t>(needed) / sizeof(proc_fdinfo) + 16);
    const int got = ::proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), static_cast<int>(fds.size() * sizeof(proc_fdinfo)));
    if (got <= 0) return {};
    fds.resize(static_cast<std::size_t>(got) / sizeof(proc_fdinfo));
    return fds;
}

[[nodiscard]] std::optional<LocalSocket> local_socket_of(pid_t pid, int fd) {
    socket_fdinfo info{};
    const int got = ::proc_pidfdinfo(pid, fd, PROC_PIDFDSOCKETINFO, &info, static_cast<int>(sizeof info));
    if (got < static_cast<int>(sizeof info)) return std::nullopt;
    const socket_info& socket = info.psi;
    if (socket.soi_family != AF_INET && socket.soi_family != AF_INET6) return std::nullopt;
    const in_sockinfo* inet = nullptr;
    LocalSocket local;
    if (socket.soi_kind == SOCKINFO_TCP) {
        local.tcp = true;
        inet = &socket.soi_proto.pri_tcp.tcpsi_ini;
    } else if (socket.soi_kind == SOCKINFO_IN && socket.soi_protocol == IPPROTO_UDP) {
        inet = &socket.soi_proto.pri_in;
    } else {
        return std::nullopt;
    }
    local.v4 = (inet->insi_vflag & INI_IPV4) != 0;
    local.v6 = (inet->insi_vflag & INI_IPV6) != 0;
    local.port = Port{from_network(static_cast<u16>(inet->insi_lport))};
    if (local.v6) {
        std::memcpy(local.address.bytes.data(), &inet->insi_laddr.ina_6, local.address.bytes.size());
    } else {
        local.address = IpAddress::v4(from_network(static_cast<u32>(inet->insi_laddr.ina_46.i46a_addr4.s_addr)));
    }
    return local;
}

[[nodiscard]] std::optional<NativePath> executable_of(pid_t pid) {
    std::array<char, PROC_PIDPATHINFO_MAXSIZE> path{};
    const int length = ::proc_pidpath(pid, path.data(), static_cast<u32>(path.size()));
    if (length <= 0) return std::nullopt;
    return NativePath{std::string(path.data(), static_cast<std::size_t>(length))};
}

// Every process with a socket `matches` accepts, then the owner among them.
[[nodiscard]] Result<std::optional<ports::PortOwner>> find_owner(UniqueFunction<bool(const LocalSocket&)> matches) {
    Result<std::vector<pid_t>> pids = all_pids();
    if (!pids) return std::unexpected(std::move(pids.error()));
    std::vector<Candidate> holders;
    for (const pid_t pid : *pids) {
        if (pid <= 0) continue;
        for (const proc_fdinfo& fd : descriptors_of(pid)) {
            if (fd.proc_fdtype != PROX_FDTYPE_SOCKET) continue;
            const std::optional<LocalSocket> socket = local_socket_of(pid, fd.proc_fd);
            if (!socket || !matches(*socket)) continue;
            holders.push_back({.pid = static_cast<u32>(pid), .exe = executable_of(pid)});
            break;
        }
    }
    return choose_owner(holders);
}

}  // namespace

Result<std::optional<ports::PortOwner>> LibprocPortInspector::tcp_owner(Endpoint local) {
    return find_owner([local](const LocalSocket& socket) { return owns_tcp(socket, local); });
}

Result<std::optional<ports::PortOwner>> LibprocPortInspector::udp_owner(Port port) {
    return find_owner([port](const LocalSocket& socket) { return owns_udp(socket, port); });
}

}  // namespace rb::os_macos::platform
