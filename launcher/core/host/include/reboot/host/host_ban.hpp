#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "reboot/host/ip_cidr.hpp"

namespace reboot::host {

// IP-first. Account ids are self-asserted, so a ban that names only one is evadable; it is kept
// and labelled so until the backend and auth DLL give a verifiable identity. With both set, the
// account id narrows the address.
struct HostBan {
    std::optional<IpCidr> address;
    std::optional<std::string> account_id;
    std::string reason;
    std::chrono::system_clock::time_point created;
    // Absent: permanent.
    std::optional<std::chrono::system_clock::time_point> expires;

    [[nodiscard]] bool evadable() const noexcept { return !address.has_value(); }
    [[nodiscard]] bool active_at(std::chrono::system_clock::time_point now) const noexcept {
        return !expires || *expires > now;
    }

    bool operator==(const HostBan&) const = default;
};

}  // namespace reboot::host
