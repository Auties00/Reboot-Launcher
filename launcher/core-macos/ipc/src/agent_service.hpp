#pragma once

#include <string_view>

#include "engine_start_rules.hpp"
#include "reboot/foundation/diag.hpp"

// C++ entry points into SMAppService (agent_service.mm), so the starter stays .cpp.
namespace reboot::os_macos::ipc {

// This process's main bundle ships Contents/Library/LaunchAgents/<plist_name>; a CLI outside a
// bundle never does.
[[nodiscard]] bool main_bundle_has_agent(std::string_view plist_name);

// SMAppService.agent(plistName:).status of the main bundle.
[[nodiscard]] AgentStatus agent_status(std::string_view plist_name);

// SMAppService.agent(plistName:).register(); blocks for as long as the system takes. A failure is
// platform.agent_register_failed for `label`, with the NSError code.
[[nodiscard]] Result<void> agent_register(std::string_view plist_name, std::string_view label);

}  // namespace reboot::os_macos::ipc
