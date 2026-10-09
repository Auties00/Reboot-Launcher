#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "socket_table.hpp"

using rb::Endpoint;
using rb::IpAddress;
using rb::Port;
using rb::u32;
using rb::u8;
using namespace rb::os_linux::platform;

namespace {

constexpr std::string_view kHeader =
    "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n";

// What the kernel prints for an address: each network-order word as a native hex integer.
std::string kernel_hex(const IpAddress& address, bool v6) {
    std::string out;
    const std::size_t first = v6 ? 0 : 12;
    for (std::size_t word = first; word < 16; word += 4) {
        u32 value = 0;
        std::memcpy(&value, address.bytes.data() + word, sizeof value);
        std::array<char, 9> text{};
        std::snprintf(text.data(), text.size(), "%08X", value);
        out += text.data();
    }
    return out;
}

std::string row(const Endpoint& local, const Endpoint& remote, int state, u32 uid, unsigned long inode, bool v6) {
    std::array<char, 16> ports{};
    std::string line = "   0: " + kernel_hex(local.address, v6);
    std::snprintf(ports.data(), ports.size(), ":%04X ", local.port.value);
    line += ports.data() + kernel_hex(remote.address, v6);
    std::snprintf(ports.data(), ports.size(), ":%04X ", remote.port.value);
    line += ports.data();
    std::array<char, 128> rest{};
    std::snprintf(rest.data(), rest.size(), "%02X 00000000:00000000 00:00000000 00000000 %5u        0 %lu 1 0000000000000000\n",
                  state, uid, inode);
    return line + rest.data();
}

Endpoint at(std::string_view address, unsigned short port) { return {*IpAddress::parse(address), Port{port}}; }

}  // namespace

TEST_CASE("tcp and tcp6 rows parse back into their endpoints", "[socket_table]") {
    const Endpoint local = at("127.0.0.1", 3551);
    const Endpoint remote = at("127.0.0.1", 40112);
    const std::string v4 = std::string(kHeader) + row(local, remote, 1, 1000, 4242, false) +
                           row(at("0.0.0.0", 7777), at("0.0.0.0", 0), kTcpListen, 1000, 777, false);
    const std::vector<SocketRecord> records = parse_proc_net(v4, false);
    REQUIRE(records.size() == 2);
    CHECK(records[0].local == local);
    CHECK(records[0].remote == remote);
    CHECK(records[0].state == 1);
    CHECK(records[0].uid == 1000);
    CHECK(records[0].inode == 4242);
    CHECK(records[1].state == kTcpListen);

    const Endpoint v6_local = at("::1", 443);
    const std::string v6 = std::string(kHeader) + row(v6_local, at("::", 0), kTcpListen, 0, 9, true);
    const std::vector<SocketRecord> v6_records = parse_proc_net(v6, true);
    REQUIRE(v6_records.size() == 1);
    CHECK(v6_records[0].local == v6_local);
}

TEST_CASE("malformed rows are skipped", "[socket_table]") {
    const std::string text = std::string(kHeader) + "   0: XYZ:1 00000000:0000 0A\n" + "garbage\n";
    CHECK(parse_proc_net(text, false).empty());
    CHECK(parse_proc_net("", false).empty());
}

TEST_CASE("wildcards are 0.0.0.0 and ::", "[socket_table]") {
    CHECK(is_wildcard(*IpAddress::parse("0.0.0.0")));
    CHECK(is_wildcard(*IpAddress::parse("::")));
    CHECK_FALSE(is_wildcard(*IpAddress::parse("127.0.0.1")));
    CHECK_FALSE(is_wildcard(*IpAddress::parse("::1")));
}

TEST_CASE("the listener owns a port over the connections it accepted", "[socket_table]") {
    const std::vector<SocketRecord> sockets{
        {at("127.0.0.1", 7777), at("127.0.0.1", 50000), 1, 1000, 11},
        {at("0.0.0.0", 7777), at("0.0.0.0", 0), kTcpListen, 1000, 12},
        {at("127.0.0.1", 8888), at("127.0.0.1", 50001), kTcpTimeWait, 0, 0},
    };
    const auto owner = find_tcp_owner(sockets, at("127.0.0.1", 7777));
    REQUIRE(owner);
    CHECK(owner->inode == 12);
    CHECK_FALSE(find_tcp_owner(sockets, at("127.0.0.1", 8888)));
    CHECK_FALSE(find_tcp_owner(sockets, at("127.0.0.1", 9999)));
}

TEST_CASE("a v6 wildcard listener covers v4, a v4 wildcard does not cover v6", "[socket_table]") {
    const std::vector<SocketRecord> dual{{at("::", 3551), at("::", 0), kTcpListen, 1000, 21}};
    CHECK(find_tcp_owner(dual, at("127.0.0.1", 3551)));
    CHECK(find_tcp_owner(dual, at("::1", 3551)));
    const std::vector<SocketRecord> v4_only{{at("0.0.0.0", 3551), at("0.0.0.0", 0), kTcpListen, 1000, 22}};
    CHECK(find_tcp_owner(v4_only, at("192.168.1.5", 3551)));
    CHECK_FALSE(find_tcp_owner(v4_only, at("::1", 3551)));
    const std::vector<SocketRecord> other_address{{at("10.0.0.2", 3551), at("0.0.0.0", 0), kTcpListen, 1000, 23}};
    CHECK_FALSE(find_tcp_owner(other_address, at("127.0.0.1", 3551)));
    CHECK(find_tcp_owner(other_address, at("0.0.0.0", 3551)));
}

TEST_CASE("udp owners and exact connections", "[socket_table]") {
    const std::vector<SocketRecord> sockets{
        {at("0.0.0.0", 7777), at("0.0.0.0", 0), 7, 1000, 31},
        {at("127.0.0.1", 40000), at("127.0.0.1", 3551), 1, 1001, 32},
    };
    const auto udp = find_udp_owner(sockets, Port{7777});
    REQUIRE(udp);
    CHECK(udp->inode == 31);
    CHECK_FALSE(find_udp_owner(sockets, Port{7778}));

    const auto peer = find_connection(sockets, at("127.0.0.1", 40000), at("127.0.0.1", 3551));
    REQUIRE(peer);
    CHECK(peer->uid == 1001);
    CHECK_FALSE(find_connection(sockets, at("127.0.0.1", 3551), at("127.0.0.1", 40000)));
}
