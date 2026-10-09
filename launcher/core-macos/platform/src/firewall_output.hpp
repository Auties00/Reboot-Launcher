#pragma once

#include <optional>
#include <string_view>

namespace reboot::os_macos::platform {

// socketfilterfw --getglobalstate: "Firewall is enabled. (State = 1)"; nullopt when unrecognised.
[[nodiscard]] std::optional<bool> firewall_enabled(std::string_view output);
// socketfilterfw --getblockall: "Block all ENABLED!" before macOS 14, "Firewall has block all state
// set to enabled." since.
[[nodiscard]] std::optional<bool> firewall_blocks_all(std::string_view output);
// socketfilterfw --getappblocked <path>: "... is blocked from receiving incoming connections." or
// "... is permitted ..."; an app the firewall does not list is not blocked.
[[nodiscard]] bool firewall_blocks_app(std::string_view output);

}  // namespace reboot::os_macos::platform
