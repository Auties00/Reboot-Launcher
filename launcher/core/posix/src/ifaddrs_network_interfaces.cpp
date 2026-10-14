#include "reboot/posix/ifaddrs_network_interfaces.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <string>

#include "reboot/posix/posix_error.hpp"

namespace rb::posix {

Result<std::vector<ports::LocalAddress>> IfaddrsNetworkInterfaces::local_addresses() {
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return std::unexpected(call_failed("getifaddrs", errno));
    const std::unique_ptr<ifaddrs, void (*)(ifaddrs*)> owned(list, freeifaddrs);

    std::vector<ports::LocalAddress> out;
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) continue;
        if ((entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0) continue;
        // sockaddr may be under-aligned for sockaddr_in, so the address is copied out.
        sockaddr_in v4{};
        std::memcpy(&v4, entry->ifa_addr, sizeof(v4));
        const IpAddress address = IpAddress::v4(ntohl(v4.sin_addr.s_addr));
        if (ports::is_lan_candidate(address))
            out.push_back(ports::LocalAddress{address, entry->ifa_name != nullptr ? entry->ifa_name : std::string()});
    }
    return out;
}

}  // namespace rb::posix
