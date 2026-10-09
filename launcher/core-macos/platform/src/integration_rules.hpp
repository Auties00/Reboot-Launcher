#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "apple_shims.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/os_services.hpp"

namespace rb::os_macos::platform {

struct SchemeHandler {
    NativePath bundle;
    std::optional<std::string> identifier;
};

// Ours when LaunchServices picks `our_bundle`; Stale when it picks another copy of this app (same
// bundle identifier); Foreign for any other app; Absent when nothing handles the scheme.
[[nodiscard]] ports::IntegrationState scheme_state(const NativePath& our_bundle,
                                                   const std::optional<std::string>& our_identifier,
                                                   const std::optional<SchemeHandler>& handler);

// A RequiresApproval agent is Ours with detail "requires_approval"; NotFound means the bundle
// lacks the plist, so nothing can be registered: Absent.
[[nodiscard]] ports::IntegrationStatus agent_integration_status(ports::IntegrationKind kind, shims::AgentStatus status);

// XPC_SERVICE_NAME when it names one of our agents; any other value was inherited from another job.
[[nodiscard]] std::optional<std::string> own_agent_label(std::optional<std::string_view> xpc_service_name);

}  // namespace rb::os_macos::platform
