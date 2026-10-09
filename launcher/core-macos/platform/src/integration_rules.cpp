#include "integration_rules.hpp"

#include "reboot/foundation/paths.hpp"
#include "reboot/os_macos/platform/mac_integration_registrar.hpp"

namespace reboot::os_macos::platform {

ports::IntegrationState scheme_state(const NativePath& our_bundle, const std::optional<std::string>& our_identifier,
                                     const std::optional<SchemeHandler>& handler) {
    if (!handler) return ports::IntegrationState::Absent;
    if (is_inside(handler->bundle, our_bundle) && is_inside(our_bundle, handler->bundle))
        return ports::IntegrationState::Ours;
    if (our_identifier && handler->identifier == our_identifier) return ports::IntegrationState::Stale;
    return ports::IntegrationState::Foreign;
}

ports::IntegrationStatus agent_integration_status(ports::IntegrationKind kind, shims::AgentStatus status) {
    ports::IntegrationStatus out{.kind = kind, .state = ports::IntegrationState::Absent, .detail = {}};
    switch (status) {
        case shims::AgentStatus::Enabled:
            out.state = ports::IntegrationState::Ours;
            break;
        case shims::AgentStatus::RequiresApproval:
            out.state = ports::IntegrationState::Ours;
            out.detail = "requires_approval";
            break;
        case shims::AgentStatus::NotRegistered:
        case shims::AgentStatus::NotFound:
            break;
    }
    return out;
}

std::optional<std::string> own_agent_label(std::optional<std::string_view> xpc_service_name) {
    if (!xpc_service_name || (*xpc_service_name != kEngineAgentLabel && *xpc_service_name != kLoginAgentLabel))
        return std::nullopt;
    return std::string(*xpc_service_name);
}

}  // namespace reboot::os_macos::platform
