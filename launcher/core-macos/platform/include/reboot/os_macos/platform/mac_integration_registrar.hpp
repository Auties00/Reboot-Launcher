#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_macos::platform {

// Agents in Contents/Library/LaunchAgents run Contents/MacOS/reboot-engine; label = name minus .plist.
// KeepAlive{Crashed=true}: SuccessfulExit would imply RunAtLoad and start this agent at every login.
inline constexpr std::string_view kEngineAgentPlist = "dev.projectreboot.launcher.engine.plist";
// core-macos/ipc kickstarts gui/<uid>/<kEngineAgentLabel> by the same name.
inline constexpr std::string_view kEngineAgentLabel = "dev.projectreboot.launcher.engine";
// The Autostart toggle: RunAtLoad, with --origin=service-manager.
inline constexpr std::string_view kLoginAgentPlist = "dev.projectreboot.launcher.engine-login.plist";
inline constexpr std::string_view kLoginAgentLabel = "dev.projectreboot.launcher.engine-login";

// Covers no capability ids; IIntegrationRegistrar for the app, which SMAppService resolves as main bundle.
class MacIntegrationRegistrar final : public ports::IIntegrationRegistrar {
public:
    // Without a bundle (a dev build) every kind is Absent; a translocated bundle cannot apply.
    MacIntegrationRegistrar(std::optional<NativePath> app_bundle, bool translocated);

    // A RequiresApproval agent is Ours with detail "requires_approval".
    Result<ports::IntegrationStatus> status(ports::IntegrationKind kind) override;
    // A Foreign reboot:// handler is never replaced (platform.integration_foreign).
    Result<void> apply(ports::IntegrationKind kind, const NativePath& exe) override;
    // UrlScheme cannot be removed: the declaration leaves only with the bundle.
    Result<void> remove(ports::IntegrationKind kind) override;

private:
    std::optional<NativePath> app_bundle_;
    bool translocated_ = false;
};

}  // namespace rb::os_macos::platform
