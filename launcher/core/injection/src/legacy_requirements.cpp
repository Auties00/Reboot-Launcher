#include "reboot/injection/legacy_requirements.hpp"

#include <algorithm>

namespace rb::injection {

bool LegacyRequirements::xmpp_available() const noexcept {
    return std::ranges::any_of(fixed_listeners, [](const Endpoint& endpoint) { return endpoint.port == kLegacyXmppPort; });
}

LegacyRequirements legacy_requirements(ports::RunnerKind runner) {
    const IpAddress loopback = IpAddress::v4(0x7F000001);
    LegacyRequirements requirements;
    requirements.fixed_listeners.push_back(Endpoint{loopback, kLegacyBackendPort});
    if (runner == ports::RunnerKind::Native) requirements.fixed_listeners.push_back(Endpoint{loopback, kLegacyXmppPort});
    return requirements;
}

}  // namespace rb::injection
