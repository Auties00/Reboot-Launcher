#pragma once

#include <vector>

#include "reboot/foundation/net_types.hpp"

struct addrinfo;

namespace rb::os_linux::platform {

// The addresses of a getaddrinfo list in its order, v4 ones IPv4-mapped, without duplicates.
[[nodiscard]] std::vector<IpAddress> addrinfo_addresses(const addrinfo* list);

}  // namespace rb::os_linux::platform
