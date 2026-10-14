#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_network_interfaces.hpp"

#include <vector>

#include "win_error.hpp"
#include "wide.hpp"

namespace rb::os_windows::platform {

Result<std::vector<ports::LocalAddress>> WindowsNetworkInterfaces::local_addresses() {
    constexpr ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    // The adapter list can grow between calls, hence the loop.
    ULONG size = 16 * 1024;
    std::vector<u64> buffer;
    ULONG error = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && error == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.assign(size / sizeof(u64) + 1, 0);
        size = static_cast<ULONG>(buffer.size() * sizeof(u64));
        error = GetAdaptersAddresses(AF_INET, kFlags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    // No adapter at all is an empty list, not a failure.
    if (error == ERROR_NO_DATA) return std::vector<ports::LocalAddress>{};
    if (error != NO_ERROR) return std::unexpected(call_failed("GetAdaptersAddresses", error));

    std::vector<ports::LocalAddress> out;
    for (auto* adapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter != nullptr;
         adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const std::string name = adapter->FriendlyName != nullptr ? narrow(adapter->FriendlyName) : std::string();
        for (const IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
             unicast = unicast->Next) {
            const SOCKADDR* socket = unicast->Address.lpSockaddr;
            if (socket == nullptr || socket->sa_family != AF_INET) continue;
            const auto* v4 = reinterpret_cast<const sockaddr_in*>(socket);
            const IpAddress address = IpAddress::v4(ntohl(v4->sin_addr.s_addr));
            if (ports::is_lan_candidate(address)) out.push_back(ports::LocalAddress{address, name});
        }
    }
    return out;
}

}  // namespace rb::os_windows::platform
