#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <vector>

#include "addrinfo_addresses.hpp"
#include "inotify_events.hpp"

using reboot::IpAddress;
using reboot::u8;
using reboot::ports::FileChangeKind;
using namespace reboot::os_linux::platform;

TEST_CASE("inotify events map to file changes", "[inotify_events]") {
    const auto entry = [](reboot::u32 mask) { return map_inotify_event(mask); };
    CHECK(entry(IN_CREATE)->kind == FileChangeKind::Created);
    CHECK(entry(IN_CREATE | IN_ISDIR)->kind == FileChangeKind::Created);
    CHECK(entry(IN_DELETE)->kind == FileChangeKind::Removed);
    CHECK(entry(IN_CLOSE_WRITE)->kind == FileChangeKind::Modified);
    CHECK(entry(IN_MODIFY)->kind == FileChangeKind::Modified);
    CHECK(entry(IN_MOVED_FROM)->kind == FileChangeKind::Renamed);
    CHECK(entry(IN_MOVED_TO)->kind == FileChangeKind::Renamed);
    CHECK(entry(IN_MOVED_TO)->subject == InotifySubject::Entry);
    CHECK(entry(IN_DELETE_SELF)->subject == InotifySubject::Directory);
    CHECK(entry(IN_MOVE_SELF)->kind == FileChangeKind::Removed);
    CHECK(entry(IN_Q_OVERFLOW)->subject == InotifySubject::AllDirectories);
    CHECK(entry(IN_Q_OVERFLOW)->kind == FileChangeKind::Modified);
    CHECK_FALSE(entry(IN_IGNORED));
    CHECK((inotify_watch_mask() & IN_ONLYDIR) != 0);
}

TEST_CASE("getaddrinfo results become IPv4-mapped addresses without duplicates", "[resolver]") {
    sockaddr_in v4{};
    v4.sin_family = AF_INET;
    const u8 loopback[4]{127, 0, 0, 1};
    std::memcpy(&v4.sin_addr, loopback, sizeof loopback);
    sockaddr_in6 v6{};
    v6.sin6_family = AF_INET6;
    v6.sin6_addr.s6_addr[15] = 1;

    addrinfo third{};
    third.ai_family = AF_INET;
    third.ai_addrlen = sizeof v4;
    third.ai_addr = reinterpret_cast<sockaddr*>(&v4);
    addrinfo second{};
    second.ai_family = AF_INET6;
    second.ai_addrlen = sizeof v6;
    second.ai_addr = reinterpret_cast<sockaddr*>(&v6);
    second.ai_next = &third;
    addrinfo first = third;
    first.ai_next = &second;

    const std::vector<IpAddress> addresses = addrinfo_addresses(&first);
    REQUIRE(addresses.size() == 2);
    CHECK(addresses[0] == *IpAddress::parse("127.0.0.1"));
    CHECK(addresses[0].is_v4());
    CHECK(addresses[1] == *IpAddress::parse("::1"));
    CHECK(addrinfo_addresses(nullptr).empty());
}
