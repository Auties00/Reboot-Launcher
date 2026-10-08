#pragma once

#include <chrono>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/host/host_ban.hpp"
#include "reboot/host/ip_cidr.hpp"

namespace reboot::host {

// Account ids are self-asserted, so remote operator rights need an address match. The optional
// engine-issued operator token of game-server-dll-design is not offered: the allowlist is all.
struct OperatorPolicy {
    // Remote operators must come from one of these; the engine channel is always trusted.
    std::vector<IpCidr> operator_cidrs;
    std::vector<HostBan> bans;

    bool operator==(const OperatorPolicy&) const = default;
};

// Clears host bits and drops exact repeats of a block or ban, keeping the first. Fails with
// host.invalid_operator_address for a prefix longer than the family allows, and
// host.ban_without_target for a ban with neither an address nor an account id.
[[nodiscard]] Result<OperatorPolicy> normalize(OperatorPolicy policy);

// What a live session's server is sent: the bans still in force at `now`.
[[nodiscard]] std::vector<HostBan> active_bans(const OperatorPolicy& policy,
                                               std::chrono::system_clock::time_point now);

}  // namespace reboot::host
