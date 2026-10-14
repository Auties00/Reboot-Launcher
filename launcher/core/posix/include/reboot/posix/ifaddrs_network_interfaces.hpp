#pragma once

#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/ports/net.hpp"

namespace rb::posix {

// Covers no capability ids; INetworkInterfaces over getifaddrs(3), named by interface (en0, zt0).
class IfaddrsNetworkInterfaces final : public ports::INetworkInterfaces {
public:
    Result<std::vector<ports::LocalAddress>> local_addresses() override;
};

}  // namespace rb::posix
