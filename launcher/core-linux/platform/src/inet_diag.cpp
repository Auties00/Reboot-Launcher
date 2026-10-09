#include "inet_diag.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "reboot/posix/unique_fd.hpp"

namespace rb::os_linux::platform {

namespace {

constexpr std::size_t align4(std::size_t length) noexcept { return (length + 3) & ~std::size_t{3}; }

constexpr std::size_t kHeaderLength = align4(sizeof(nlmsghdr));
constexpr std::size_t kReceiveBuffer = 64 * 1024;
// A kernel that never answers must not hold a worker for long.
constexpr long kReceiveTimeoutSeconds = 2;

struct DiagRequest {
    nlmsghdr header;
    inet_diag_req_v2 body;
};

[[nodiscard]] u16 from_network(u16 raw) noexcept {
    std::array<u8, 2> bytes{};
    std::memcpy(bytes.data(), &raw, sizeof raw);
    return static_cast<u16>((bytes[0] << 8) | bytes[1]);
}

[[nodiscard]] u16 to_network(u16 value) noexcept {
    const std::array<u8, 2> bytes{static_cast<u8>(value >> 8), static_cast<u8>(value)};
    u16 raw = 0;
    std::memcpy(&raw, bytes.data(), sizeof raw);
    return raw;
}

[[nodiscard]] Endpoint endpoint_of(u8 family, const void* address, u16 port) noexcept {
    Endpoint endpoint;
    endpoint.port = Port{from_network(port)};
    auto& bytes = endpoint.address.bytes;
    if (family == AF_INET) {
        bytes[10] = 0xFF;
        bytes[11] = 0xFF;
        std::memcpy(bytes.data() + 12, address, 4);
    } else {
        std::memcpy(bytes.data(), address, 16);
    }
    return endpoint;
}

void put_address(u8 family, const IpAddress& address, void* out) noexcept {
    if (family == AF_INET)
        std::memcpy(out, address.bytes.data() + 12, 4);
    else
        std::memcpy(out, address.bytes.data(), 16);
}

[[nodiscard]] SocketRecord record_of(const inet_diag_msg& message) noexcept {
    SocketRecord record;
    record.local = endpoint_of(message.idiag_family, message.id.idiag_src, message.id.idiag_sport);
    record.remote = endpoint_of(message.idiag_family, message.id.idiag_dst, message.id.idiag_dport);
    record.state = message.idiag_state;
    record.uid = message.idiag_uid;
    record.inode = message.idiag_inode;
    return record;
}

[[nodiscard]] posix::UniqueFd open_diag_socket() {
    posix::UniqueFd fd{::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG)};
    if (!fd.valid()) return fd;
    timeval timeout{};
    timeout.tv_sec = kReceiveTimeoutSeconds;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) != 0) fd.reset();
    return fd;
}

[[nodiscard]] DiagRequest make_request(u8 family, u8 protocol, bool dump) noexcept {
    DiagRequest request{};
    request.header.nlmsg_len = sizeof request;
    request.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.header.nlmsg_flags = static_cast<u16>(NLM_F_REQUEST | (dump ? NLM_F_DUMP : 0));
    request.header.nlmsg_seq = 1;
    request.body.sdiag_family = family;
    request.body.sdiag_protocol = protocol;
    request.body.idiag_states = ~0U;
    request.body.id.idiag_cookie[0] = INET_DIAG_NOCOOKIE;
    request.body.id.idiag_cookie[1] = INET_DIAG_NOCOOKIE;
    return request;
}

[[nodiscard]] bool send_request(int fd, const DiagRequest& request) {
    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    for (;;) {
        const ssize_t sent = ::sendto(fd, &request, sizeof request, 0, reinterpret_cast<const sockaddr*>(&kernel),
                                      sizeof kernel);
        if (sent == static_cast<ssize_t>(sizeof request)) return true;
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }
}

// Collects SOCK_DIAG_BY_FAMILY answers until NLMSG_DONE, or the first one when not dumping.
// 0, or the errno of an NLMSG_ERROR or of a failed receive.
[[nodiscard]] int receive(int fd, bool dump, std::vector<SocketRecord>& out) {
    std::vector<char> buffer(kReceiveBuffer);
    for (;;) {
        const ssize_t got = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            return errno;
        }
        const auto total = static_cast<std::size_t>(got);
        std::size_t offset = 0;
        while (offset + sizeof(nlmsghdr) <= total) {
            nlmsghdr header{};
            std::memcpy(&header, buffer.data() + offset, sizeof header);
            if (header.nlmsg_len < sizeof(nlmsghdr) || offset + header.nlmsg_len > total) return EBADMSG;
            if (header.nlmsg_type == NLMSG_DONE) return 0;
            if (header.nlmsg_type == NLMSG_ERROR) {
                if (header.nlmsg_len < kHeaderLength + sizeof(nlmsgerr)) return EBADMSG;
                nlmsgerr error{};
                std::memcpy(&error, buffer.data() + offset + kHeaderLength, sizeof error);
                return -error.error;
            }
            if (header.nlmsg_type == SOCK_DIAG_BY_FAMILY && header.nlmsg_len >= kHeaderLength + sizeof(inet_diag_msg)) {
                inet_diag_msg message{};
                std::memcpy(&message, buffer.data() + offset + kHeaderLength, sizeof message);
                out.push_back(record_of(message));
            }
            offset += align4(header.nlmsg_len);
        }
        if (!dump && !out.empty()) return 0;
    }
}

}  // namespace

std::optional<std::vector<SocketRecord>> diag_dump(DiagProtocol protocol) {
    const posix::UniqueFd fd = open_diag_socket();
    if (!fd.valid()) return std::nullopt;
    const auto ip_protocol = static_cast<u8>(protocol == DiagProtocol::Tcp ? IPPROTO_TCP : IPPROTO_UDP);
    std::vector<SocketRecord> records;
    for (const u8 family : {static_cast<u8>(AF_INET), static_cast<u8>(AF_INET6)}) {
        const DiagRequest request = make_request(family, ip_protocol, true);
        if (!send_request(fd.get(), request) || receive(fd.get(), true, records) != 0) return std::nullopt;
    }
    return records;
}

DiagLookup diag_find_tcp(Endpoint local, Endpoint remote) {
    const posix::UniqueFd fd = open_diag_socket();
    if (!fd.valid()) return {};
    const auto family = static_cast<u8>(local.address.is_v4() && remote.address.is_v4() ? AF_INET : AF_INET6);
    DiagRequest request = make_request(family, static_cast<u8>(IPPROTO_TCP), false);
    request.body.id.idiag_sport = to_network(local.port.value);
    request.body.id.idiag_dport = to_network(remote.port.value);
    put_address(family, local.address, request.body.id.idiag_src);
    put_address(family, remote.address, request.body.id.idiag_dst);
    if (!send_request(fd.get(), request)) return {};
    std::vector<SocketRecord> found;
    const int error = receive(fd.get(), false, found);
    if (error == ENOENT) return {.answered = true};
    if (error != 0 || found.empty()) return {};
    return {.answered = true, .socket = found.front()};
}

}  // namespace rb::os_linux::platform
