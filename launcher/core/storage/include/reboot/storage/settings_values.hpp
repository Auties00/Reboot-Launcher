#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/backend_target.hpp"
#include "reboot/storage/console_key.hpp"
#include "reboot/storage/enum_names.hpp"

// Typed settings. Member initialisers are the defaults every key resets to.
namespace reboot::storage {

enum class Theme : u8 { System, Light, Dark };
enum class HostListing : u8 { Listed, Unlisted };
// AfterMatch drains a running host before an update applies; Manual waits for the user to stop it.
enum class HostUpdatePolicy : u8 { AfterMatch, Manual };
enum class UpdateChannel : u8 { Stable, Beta };

template <>
struct EnumNames<Theme> {
    static constexpr std::array<std::string_view, 3> kNames{"system", "light", "dark"};
};
template <>
struct EnumNames<HostListing> {
    static constexpr std::array<std::string_view, 2> kNames{"listed", "unlisted"};
};
template <>
struct EnumNames<HostUpdatePolicy> {
    static constexpr std::array<std::string_view, 2> kNames{"after_match", "manual"};
};
template <>
struct EnumNames<UpdateChannel> {
    static constexpr std::array<std::string_view, 2> kNames{"stable", "beta"};
};

inline constexpr std::string_view kSystemLanguage = "system";

struct ClientSettings {
    // The culture the game starts with, separate from ui.language.
    std::string game_culture{kSystemLanguage};

    bool operator==(const ClientSettings&) const = default;
};

struct PlaySettings {
    // Appended to the launch argv after the fixed and -AUTH_* tokens; quoting is parsed at launch.
    std::string custom_args;
    // Set means NetMode::LegacyFixed for every play session.
    std::optional<NativePath> custom_auth_dll;
    // NAME=value lines for EnvBuilder's pass-through layer, which drops names outside its list.
    std::string env;
    // PROTON_LOG into the session's Wine log folder; Wine runners only.
    bool verbose_wine_log = false;

    bool operator==(const PlaySettings&) const = default;
};

struct BackendSettings {
    BackendTarget target;
    // The embedded backend binds 0.0.0.0 instead of 127.0.0.1.
    bool allow_lan = false;
    ConsoleKey console_key;

    bool operator==(const BackendSettings&) const = default;
};

struct HostSettings {
    HostUpdatePolicy update_policy = HostUpdatePolicy::AfterMatch;
    // What a new host profile starts with; each profile then keeps its own.
    HostListing listing = HostListing::Unlisted;

    bool operator==(const HostSettings&) const = default;
};

struct UpdateSettings {
    UpdateChannel channel = UpdateChannel::Stable;
    bool auto_check = true;

    bool operator==(const UpdateSettings&) const = default;
};

struct UiSettings {
    std::string language{kSystemLanguage};
    Theme theme = Theme::System;

    bool operator==(const UiSettings&) const = default;
};

struct SettingsValues {
    ClientSettings client;
    PlaySettings play;
    BackendSettings backend;
    HostSettings host;
    UpdateSettings updates;
    UiSettings ui;

    bool operator==(const SettingsValues&) const = default;
};

}  // namespace reboot::storage
