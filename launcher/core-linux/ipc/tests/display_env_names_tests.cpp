#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>

#include "display_env_names.hpp"

using rb::os_linux::ipc::is_display_env_name;

TEST_CASE("display, session bus and locale variables reach the engine", "[display_env_names]") {
    for (const std::string_view name : {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE",
                                        "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP", "PULSE_SERVER",
                                        "DBUS_SESSION_BUS_ADDRESS", "LANG", "LC_ALL", "LC_MESSAGES",
                                        "PIPEWIRE_RUNTIME_DIR"}) {
        INFO(name);
        CHECK(is_display_env_name(name));
    }
}

TEST_CASE("loader, Steam, Wine and launcher variables never do", "[display_env_names]") {
    for (const std::string_view name : {"LD_PRELOAD", "LD_LIBRARY_PATH", "SteamAppId", "STEAM_COMPAT_DATA_PATH",
                                        "WINEPREFIX", "UMU_ID", "REBOOT_LAUNCHER_HOME", "APPIMAGE", "PATH", "HOME",
                                        "display", "DISPLAY_X", "LANGUAGE", "LC", ""}) {
        INFO(name);
        CHECK_FALSE(is_display_env_name(name));
    }
}
