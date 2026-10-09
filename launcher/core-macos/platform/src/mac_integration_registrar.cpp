#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_integration_registrar.hpp"

#include <utility>

#include "apple_shims.hpp"
#include "integration_rules.hpp"
#include "messages.hpp"
#include "reboot/foundation/paths.hpp"

namespace rb::os_macos::platform {

namespace {

constexpr std::string_view kScheme = "reboot";

[[nodiscard]] std::optional<std::string_view> agent_plist(ports::IntegrationKind kind) {
    switch (kind) {
        case ports::IntegrationKind::Autostart:
            return kLoginAgentPlist;
        case ports::IntegrationKind::EngineAgent:
            return kEngineAgentPlist;
        case ports::IntegrationKind::UrlScheme:
        case ports::IntegrationKind::DesktopEntry:
            break;
    }
    return std::nullopt;
}

[[nodiscard]] Result<void> not_supported() {
    return make_diag(ErrorDomain::Platform, kNotSupported).kind(ErrorKind::Unsupported).fail();
}

[[nodiscard]] ports::IntegrationStatus scheme_status(const NativePath& bundle) {
    std::optional<SchemeHandler> handler;
    if (std::optional<NativePath> handler_bundle = shims::default_handler_bundle(kScheme))
        handler = SchemeHandler{*handler_bundle, shims::bundle_identifier(*handler_bundle)};
    ports::IntegrationStatus status{.kind = ports::IntegrationKind::UrlScheme,
                                    .state = scheme_state(bundle, shims::bundle_identifier(bundle), handler),
                                    .detail = {}};
    if (handler) status.detail = display_utf8(handler->bundle);
    return status;
}

}  // namespace

MacIntegrationRegistrar::MacIntegrationRegistrar(std::optional<NativePath> app_bundle, bool translocated)
    : app_bundle_(std::move(app_bundle)), translocated_(translocated) {}

Result<ports::IntegrationStatus> MacIntegrationRegistrar::status(ports::IntegrationKind kind) {
    if (!app_bundle_ || kind == ports::IntegrationKind::DesktopEntry)
        return ports::IntegrationStatus{.kind = kind, .state = ports::IntegrationState::Absent, .detail = {}};
    if (kind == ports::IntegrationKind::UrlScheme) return scheme_status(*app_bundle_);
    return agent_integration_status(kind, shims::agent_status(*agent_plist(kind)));
}

Result<void> MacIntegrationRegistrar::apply(ports::IntegrationKind kind, const NativePath& exe) {
    if (kind == ports::IntegrationKind::DesktopEntry) return not_supported();
    if (!app_bundle_ || !is_inside(exe, *app_bundle_))
        return make_diag(ErrorDomain::Platform, kNotInBundle).arg("path", exe).kind(ErrorKind::InvalidInput).fail();
    if (translocated_) return make_diag(ErrorDomain::Platform, kAppTranslocated).kind(ErrorKind::Conflict).fail();

    if (kind == ports::IntegrationKind::UrlScheme) {
        const ports::IntegrationStatus current = scheme_status(*app_bundle_);
        if (current.state == ports::IntegrationState::Foreign)
            return make_diag(ErrorDomain::Platform, kIntegrationForeign)
                .arg("entry", std::string(kScheme) + "://")
                .kind(ErrorKind::Conflict)
                .fail();
        if (current.state == ports::IntegrationState::Ours) return {};
        if (Result<void> registered = shims::register_bundle(*app_bundle_); !registered) return registered;
        return shims::set_default_handler(*app_bundle_, kScheme);
    }

    // An agent waiting for the user's approval in Login Items is registered; only they can enable it.
    const std::string_view plist = *agent_plist(kind);
    const shims::AgentStatus before = shims::agent_status(plist);
    if (before == shims::AgentStatus::Enabled || before == shims::AgentStatus::RequiresApproval) return {};
    Result<void> registered = shims::agent_register(plist);
    // Registering a login item the user turned off fails, yet leaves it waiting for approval.
    if (!registered && shims::agent_status(plist) == shims::AgentStatus::RequiresApproval) return {};
    return registered;
}

Result<void> MacIntegrationRegistrar::remove(ports::IntegrationKind kind) {
    switch (kind) {
        case ports::IntegrationKind::UrlScheme:
            return not_supported();
        case ports::IntegrationKind::DesktopEntry:
            return {};
        case ports::IntegrationKind::Autostart:
        case ports::IntegrationKind::EngineAgent:
            break;
    }
    // SMAppService addresses agents through the main bundle; a dev build has none registered.
    if (!app_bundle_) return {};
    const std::string_view plist = *agent_plist(kind);
    const shims::AgentStatus current = shims::agent_status(plist);
    if (current == shims::AgentStatus::NotRegistered || current == shims::AgentStatus::NotFound) return {};
    return shims::agent_unregister(plist);
}

}  // namespace rb::os_macos::platform
