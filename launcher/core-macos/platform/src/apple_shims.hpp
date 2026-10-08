#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

// C++ entry points into the Objective-C++ shims (src/*.mm), so the adapters stay .cpp.
namespace reboot::os_macos::platform::shims {

// NSWorkspace and NSFileManager, for MacShell and the prerequisite settings panes.
[[nodiscard]] Result<void> workspace_open(std::string_view url);
[[nodiscard]] Result<void> workspace_open_file(const NativePath& path);
[[nodiscard]] Result<void> workspace_reveal(const NativePath& path);
[[nodiscard]] Result<void> file_manager_trash(const NativePath& path);

// LaunchServices, for the reboot:// scheme.
[[nodiscard]] std::optional<NativePath> default_handler_bundle(std::string_view scheme);
[[nodiscard]] Result<void> register_bundle(const NativePath& app_bundle);
[[nodiscard]] Result<void> set_default_handler(const NativePath& app_bundle, std::string_view scheme);

// Read from Contents/Info.plist on disk: NSBundle and CFBundle cache per path and miss a swap.
[[nodiscard]] std::optional<std::string> bundle_identifier(const NativePath& app_bundle);
[[nodiscard]] std::optional<std::string> bundle_version(const NativePath& app_bundle);

enum class AgentStatus : u8 { NotRegistered, Enabled, RequiresApproval, NotFound };

// SMAppService.agent(plistName:) of the process's main bundle.
[[nodiscard]] AgentStatus agent_status(std::string_view plist_name);
[[nodiscard]] Result<void> agent_register(std::string_view plist_name);
[[nodiscard]] Result<void> agent_unregister(std::string_view plist_name);

struct VolumeKeys {
    std::string localized_name;
    // Absent on volumes that do not report NSURLVolumeAvailableCapacityForImportantUsageKey.
    std::optional<u64> important_free_bytes;
    bool removable = false;
    bool local = true;
};

[[nodiscard]] std::optional<VolumeKeys> volume_keys(const NativePath& mount);

// MTLCreateSystemDefaultDevice supportsFamily:MTLGPUFamilyMetal3.
[[nodiscard]] bool metal3_supported();

}  // namespace reboot::os_macos::platform::shims
