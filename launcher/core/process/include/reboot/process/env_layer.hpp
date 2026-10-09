#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "reboot/contracts/game_client.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::process {

// In application order: a later layer overrides an earlier one, and kDenyList runs last over all of them.
enum class EnvLayer : u8 { DaemonBase, ClientAllowList, ProfilePassThrough, Runner, Channel };

// Windows names compare case-insensitively and keep the platform's whole base; POSIX names are exact.
enum class EnvSyntax : u8 { Posix, Windows };

struct EnvNamePattern {
    std::string_view text;
    // Matches every name that starts with `text`.
    bool prefix = false;

    [[nodiscard]] constexpr bool matches(std::string_view name, EnvSyntax syntax) const noexcept {
        if (prefix ? name.size() < text.size() : name.size() != text.size()) return false;
        for (std::size_t i = 0; i < text.size(); ++i) {
            char a = name[i];
            char b = text[i];
            if (syntax == EnvSyntax::Windows) {
                if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
                if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
            }
            if (a != b) return false;
        }
        return true;
    }
};

// Layer 1 on POSIX: process-model's passwd fields plus linux-compat-layer 4(a). On Windows the
// platform supplies the CreateEnvironmentBlock set unfiltered.
inline constexpr std::array<EnvNamePattern, 9> kPosixBaseNames{{
    {"HOME"},
    {"USER"},
    {"LOGNAME"},
    {"SHELL"},
    {"PATH"},
    {"TMPDIR"},
    {"XDG_DATA_HOME"},
    {"XDG_CACHE_HOME"},
    {"XDG_CONFIG_HOME"},
}};

// Layer 2: what CallerContext.display_env may contribute.
inline constexpr std::array<EnvNamePattern, 12> kClientAllowList{{
    {"DISPLAY"},
    {"WAYLAND_DISPLAY"},
    {"XAUTHORITY"},
    {"XDG_RUNTIME_DIR"},
    {"XDG_SESSION_TYPE"},
    {"XDG_CURRENT_DESKTOP"},
    {"XDG_SESSION_DESKTOP"},
    {"PULSE_SERVER"},
    {"PIPEWIRE_", true},
    {"DBUS_SESSION_BUS_ADDRESS"},
    {"LANG"},
    {"LC_", true},
}};

// Layer 3: what a profile's user settings may contribute.
inline constexpr std::array<EnvNamePattern, 9> kProfilePassThrough{{
    {"DRI_PRIME"},
    {"__NV_PRIME_RENDER_OFFLOAD"},
    {"__GLX_VENDOR_LIBRARY_NAME"},
    {"__VK_LAYER_NV_optimus"},
    {"MESA_VK_DEVICE_SELECT"},
    {"DXVK_", true},
    {"VKD3D_", true},
    {"PROTON_", true},
    {"WINEDEBUG"},
}};

struct EnvDenyRule {
    EnvNamePattern name;
    // The one layer the name may still come from; none means it is removed whatever set it.
    std::optional<EnvLayer> exempt;
};

// REBOOT_* from any layer but Channel is stray and removed.
inline constexpr std::array<EnvDenyRule, 11> kDenyList{{
    {{"LD_PRELOAD"}, std::nullopt},
    {{"LD_LIBRARY_PATH"}, std::nullopt},
    {{"APPDIR"}, std::nullopt},
    {{"APPIMAGE"}, std::nullopt},
    {{"OWD"}, std::nullopt},
    {{"Steam", true}, std::nullopt},
    {{"STEAM_COMPAT_", true}, std::nullopt},
    {{"WINEPREFIX"}, EnvLayer::Runner},
    {{"PROTONPATH"}, EnvLayer::Runner},
    {{"UMU_", true}, EnvLayer::Runner},
    {{"REBOOT_", true}, EnvLayer::Channel},
}};

// The only channel variables a Wine launch may carry (credential-security item 7): the client DLL
// and winhost bootstrap names.
inline constexpr std::array<EnvNamePattern, 4> kWineChannelNames{{
    {contracts::game_client::kEnvCtl},
    {contracts::game_client::kEnvCtlToken},
    {contracts::game_client::kEnvSession},
    {contracts::game_client::kEnvRole},
}};

}  // namespace rb::process
