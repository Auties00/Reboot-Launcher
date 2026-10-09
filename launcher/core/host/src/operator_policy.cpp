#include "reboot/host/operator_policy.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "reboot/host/host_error.hpp"

namespace rb::host {

namespace {

[[nodiscard]] Result<IpCidr> checked(const IpCidr& cidr) {
    if (cidr.prefix > cidr.max_prefix()) {
        return std::unexpected(
            to_diagnostic(HostError{.code = HostErrorCode::InvalidOperatorAddress,
                                    .address = cidr.address.to_string() + "/" + std::to_string(cidr.prefix)}));
    }
    return cidr.canonical();
}

template <class T>
void append_unique(std::vector<T>& out, T value) {
    if (std::ranges::find(out, value) == out.end()) out.push_back(std::move(value));
}

}  // namespace

Result<OperatorPolicy> normalize(OperatorPolicy policy) {
    OperatorPolicy out;
    for (const IpCidr& cidr : policy.operator_cidrs) {
        auto canonical = checked(cidr);
        if (!canonical) return std::unexpected(std::move(canonical.error()));
        append_unique(out.operator_cidrs, *canonical);
    }
    for (HostBan& ban : policy.bans) {
        if (ban.account_id && ban.account_id->empty()) ban.account_id.reset();
        if (!ban.address && !ban.account_id)
            return std::unexpected(to_diagnostic(HostError{.code = HostErrorCode::BanWithoutTarget}));
        if (ban.address) {
            auto canonical = checked(*ban.address);
            if (!canonical) return std::unexpected(std::move(canonical.error()));
            ban.address = *canonical;
        }
        append_unique(out.bans, std::move(ban));
    }
    return out;
}

std::vector<HostBan> active_bans(const OperatorPolicy& policy, std::chrono::system_clock::time_point now) {
    std::vector<HostBan> out;
    for (const HostBan& ban : policy.bans)
        if (ban.active_at(now)) out.push_back(ban);
    return out;
}

}  // namespace rb::host
