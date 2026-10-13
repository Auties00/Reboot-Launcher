#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace rb::os_macos::platform {

// The firewall's settings, tried in order: System Settings > Network > Firewall since macOS 13,
// then the pre-13 Security pane anchor, which macOS 14 opens on Privacy & Security instead.
// System Settings takes the first URL even when a release drops its Firewall anchor (reported for
// macOS 26) and then shows Network, which lists Firewall; the second runs only if openURL refuses.
inline constexpr std::array<std::string_view, 2> kFirewallPanes{
    "x-apple.systempreferences:com.apple.Network-Settings.extension?Firewall",
    "x-apple.systempreferences:com.apple.preference.security?Firewall",
};

// socketfilterfw --getglobalstate: "Firewall is enabled. (State = 1)"; nullopt when unrecognised.
[[nodiscard]] std::optional<bool> firewall_enabled(std::string_view output);
// socketfilterfw --getblockall: "Block all ENABLED!" before macOS 14, "Firewall has block all state
// set to enabled." since.
[[nodiscard]] std::optional<bool> firewall_blocks_all(std::string_view output);
// socketfilterfw --getappblocked <path>: "... is blocked from receiving incoming connections." or
// "... is permitted ..."; an app the firewall does not list is not blocked.
[[nodiscard]] bool firewall_blocks_app(std::string_view output);

}  // namespace rb::os_macos::platform
