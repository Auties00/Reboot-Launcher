#pragma once

#include <array>
#include <string_view>

namespace rb::os_linux::ipc {

// Must equal process::kClientAllowList, linux-compat-layer's client allow-list, which this
// package may not include. Steam*, LD_*, WINE* and UMU_* are never on it.
inline constexpr std::array<std::string_view, 10> kDisplayEnvNames{
    "DISPLAY",
    "WAYLAND_DISPLAY",
    "XAUTHORITY",
    "XDG_RUNTIME_DIR",
    "XDG_SESSION_TYPE",
    "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_DESKTOP",
    "PULSE_SERVER",
    "DBUS_SESSION_BUS_ADDRESS",
    "LANG",
};
inline constexpr std::array<std::string_view, 2> kDisplayEnvPrefixes{"PIPEWIRE_", "LC_"};

// Whether LinuxCallerContext puts `name` into CallerContext::display_env.
[[nodiscard]] constexpr bool is_display_env_name(std::string_view name) noexcept {
    for (std::string_view allowed : kDisplayEnvNames)
        if (name == allowed) return true;
    for (std::string_view prefix : kDisplayEnvPrefixes)
        if (name.starts_with(prefix)) return true;
    return false;
}

}  // namespace rb::os_linux::ipc
