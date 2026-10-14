#pragma once

#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; INetworkInterfaces over GetAdaptersAddresses, with each adapter's
// friendly name.
class WindowsNetworkInterfaces final : public ports::INetworkInterfaces {
public:
    Result<std::vector<ports::LocalAddress>> local_addresses() override;
};

}  // namespace rb::os_windows::platform
