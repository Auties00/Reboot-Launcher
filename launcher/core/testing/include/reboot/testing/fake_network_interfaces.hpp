#pragma once

#include <mutex>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/ports/net.hpp"

namespace rb::testing {

// Covers no capability ids (decision testing-strategy).
// INetworkInterfaces answering what the test set: by default one Ethernet adapter at 192.168.1.20.
class FakeNetworkInterfaces final : public ports::INetworkInterfaces {
public:
    Result<std::vector<ports::LocalAddress>> local_addresses() override;

    void set(Result<std::vector<ports::LocalAddress>> answer);

private:
    mutable std::mutex mutex_;
    Result<std::vector<ports::LocalAddress>> answer_{
        std::vector<ports::LocalAddress>{{IpAddress::v4(0xC0A80114), "Ethernet"}}};
};

}  // namespace rb::testing
