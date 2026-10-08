#include "reboot/host/port_policy.hpp"

#include "reboot/host/host_error.hpp"

namespace reboot::host {

namespace {

[[nodiscard]] Result<void> fail(const HostError& error) { return std::unexpected(to_diagnostic(error)); }

}  // namespace

Result<void> validate(const PortPolicy& policy) {
    if (const auto* pinned = std::get_if<PinnedPorts>(&policy)) {
        if (pinned->first < kMinHostPort) return fail({.code = HostErrorCode::InvalidPortPolicy});
        if (pinned->first == kReservedBackendPort)
            return fail({.code = HostErrorCode::ReservedPort, .port = kReservedBackendPort});
        return {};
    }
    const PortRange& range = std::get<AutoPorts>(policy).range;
    if (range.first < kMinHostPort || range.first > range.last)
        return fail({.code = HostErrorCode::InvalidPortPolicy});
    return {};
}

}  // namespace reboot::host
