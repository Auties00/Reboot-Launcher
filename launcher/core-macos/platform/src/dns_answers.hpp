#pragma once

#include <string_view>
#include <vector>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_macos::platform {

enum class DnsFamily : u8 { V4, V6 };

enum class SpecialName : u8 { None, Loopback, Invalid };

// RFC 6761: "localhost" and names under it are loopback, names under "invalid" never resolve;
// neither is sent to DNS.
[[nodiscard]] SpecialName special_name(std::string_view host) noexcept;

// Collects DNSServiceGetAddrInfo answers for both families. With ReturnIntermediates every family
// ends in an address or a negative answer, so a lookup is complete once each family answered.
class DnsAnswers {
public:
    void add(const IpAddress& address);
    void none(DnsFamily family) noexcept;

    [[nodiscard]] bool complete() const noexcept { return v4_answered_ && v6_answered_; }
    [[nodiscard]] const std::vector<IpAddress>& addresses() const noexcept { return addresses_; }

private:
    std::vector<IpAddress> addresses_;
    bool v4_answered_ = false;
    bool v6_answered_ = false;
};

}  // namespace reboot::os_macos::platform
