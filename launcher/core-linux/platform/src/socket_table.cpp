#include "socket_table.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <system_error>
#include <utility>

namespace rb::os_linux::platform {

namespace {

[[nodiscard]] std::vector<std::string_view> fields_of(std::string_view line) {
    std::vector<std::string_view> fields;
    while (!line.empty()) {
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string_view::npos) break;
        line.remove_prefix(start);
        const std::size_t end = line.find_first_of(" \t");
        fields.push_back(line.substr(0, end));
        if (end == std::string_view::npos) break;
        line.remove_prefix(end);
    }
    return fields;
}

template <class T>
[[nodiscard]] bool parse(std::string_view text, T& out, int base) noexcept {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return error == std::errc{} && end == text.data() + text.size();
}

// "0100007F:1F90", or 32 hex digits and a port for v6.
[[nodiscard]] std::optional<Endpoint> parse_endpoint(std::string_view text, bool v6) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const std::string_view address = text.substr(0, colon);
    u16 port = 0;
    if (!parse(text.substr(colon + 1), port, 16)) return std::nullopt;

    Endpoint endpoint;
    endpoint.port = Port{port};
    const std::size_t words = v6 ? 4 : 1;
    if (address.size() != words * 8) return std::nullopt;
    std::array<u8, 16>& bytes = endpoint.address.bytes;
    const std::size_t first = v6 ? 0 : 12;
    if (!v6) {
        bytes[10] = 0xFF;
        bytes[11] = 0xFF;
    }
    for (std::size_t word = 0; word < words; ++word) {
        u32 value = 0;
        if (!parse(address.substr(word * 8, 8), value, 16)) return std::nullopt;
        // The kernel printed a network-order word as a native integer.
        std::memcpy(bytes.data() + first + word * 4, &value, sizeof value);
    }
    return endpoint;
}

}  // namespace

std::vector<SocketRecord> parse_proc_net(std::string_view text, bool v6) {
    std::vector<SocketRecord> records;
    bool header = true;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (std::exchange(header, false)) continue;

        // sl local rem st tx:rx tr:when retrnsmt uid timeout inode ...
        const std::vector<std::string_view> fields = fields_of(line);
        if (fields.size() < 10) continue;
        const std::optional<Endpoint> local = parse_endpoint(fields[1], v6);
        const std::optional<Endpoint> remote = parse_endpoint(fields[2], v6);
        SocketRecord record;
        if (!local || !remote || !parse(fields[3], record.state, 16) || !parse(fields[7], record.uid, 10) ||
            !parse(fields[9], record.inode, 10))
            continue;
        record.local = *local;
        record.remote = *remote;
        records.push_back(record);
    }
    return records;
}

bool is_wildcard(const IpAddress& address) noexcept {
    const auto& bytes = address.bytes;
    if (address.is_v4()) return bytes[12] == 0 && bytes[13] == 0 && bytes[14] == 0 && bytes[15] == 0;
    return std::ranges::all_of(bytes, [](u8 byte) { return byte == 0; });
}

std::optional<SocketRecord> find_tcp_owner(const std::vector<SocketRecord>& sockets, Endpoint local) {
    const auto bound_here = [&](const SocketRecord& socket) {
        if (socket.inode == 0 || socket.local.port != local.port) return false;
        if (is_wildcard(local.address) || socket.local.address == local.address) return true;
        if (!is_wildcard(socket.local.address)) return false;
        // 0.0.0.0 covers v4 addresses only; :: covers both.
        return !socket.local.address.is_v4() || local.address.is_v4();
    };
    std::optional<SocketRecord> other;
    for (const SocketRecord& socket : sockets) {
        if (!bound_here(socket)) continue;
        if (socket.state == kTcpListen) return socket;
        if (!other && socket.state != kTcpTimeWait) other = socket;
    }
    return other;
}

std::optional<SocketRecord> find_udp_owner(const std::vector<SocketRecord>& sockets, Port port) {
    for (const SocketRecord& socket : sockets)
        if (socket.inode != 0 && socket.local.port == port) return socket;
    return std::nullopt;
}

std::optional<SocketRecord> find_connection(const std::vector<SocketRecord>& sockets, Endpoint local, Endpoint remote) {
    for (const SocketRecord& socket : sockets)
        if (socket.local == local && socket.remote == remote) return socket;
    return std::nullopt;
}

}  // namespace rb::os_linux::platform
