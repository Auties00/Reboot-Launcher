#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/storage/backend_target.hpp"
#include "reboot/storage/console_key.hpp"
#include "reboot/storage/key.hpp"
#include "reboot/storage/settings_values.hpp"

// Capabilities: settings-storage.game-store, settings-storage.backend-store, settings-storage.dll-store,
// settings-storage.app-store.
// Every key, in display order.
namespace rb::storage {

// "system" or a well-formed BCP 47 tag, kept as written; storage.invalid_language_tag.
[[nodiscard]] Result<std::string> validate_language_tag(std::string tag);
// Rejects invalid UTF-8, control characters but tab, and unbalanced quotes; storage.invalid_launch_args.
[[nodiscard]] Result<std::string> validate_launch_args(std::string args);
// Absolute and ending in .dll; existence is checked at launch. storage.invalid_auth_dll_path.
[[nodiscard]] Result<std::optional<NativePath>> validate_auth_dll_path(std::optional<NativePath> path);
// play.env: one NAME=value per line, blank lines skipped, every entry kept in order. A name is
// [A-Za-z_][A-Za-z0-9_]*; a value has no control character but tab. storage.invalid_env_line.
[[nodiscard]] Result<ports::EnvBlock> parse_env_lines(std::string_view text);
[[nodiscard]] Result<std::string> validate_env_lines(std::string text);
[[nodiscard]] Result<ConsoleKey> validate_console_key(ConsoleKey key);

namespace keys {

inline constexpr Key<std::string> kClientGameCulture{
    {.id = "client.game_culture",
     .label = MessageId{"storage.setting_client_game_culture"},
     .reset_group = ResetGroup::Play},
    settings_field<&SettingsValues::client, &ClientSettings::game_culture>(),
    &validate_language_tag};

inline constexpr Key<std::string> kPlayCustomArgs{
    {.id = "play.custom_args",
     .label = MessageId{"storage.setting_play_custom_args"},
     .sensitivity = Sensitivity::Personal,
     .reset_group = ResetGroup::Play},
    settings_field<&SettingsValues::play, &PlaySettings::custom_args>(),
    &validate_launch_args};

inline constexpr Key<std::optional<NativePath>> kPlayCustomAuthDll{
    {.id = "play.custom_auth_dll",
     .label = MessageId{"storage.setting_play_custom_auth_dll"},
     .sensitivity = Sensitivity::Personal,
     .reset_group = ResetGroup::Play},
    settings_field<&SettingsValues::play, &PlaySettings::custom_auth_dll>(),
    &validate_auth_dll_path};

inline constexpr Key<std::string> kPlayEnv{
    {.id = "play.env",
     .label = MessageId{"storage.setting_play_env"},
     .sensitivity = Sensitivity::Personal,
     .reset_group = ResetGroup::Play},
    settings_field<&SettingsValues::play, &PlaySettings::env>(),
    &validate_env_lines};

inline constexpr Key<bool> kPlayVerboseWineLog{
    {.id = "play.verbose_wine_log",
     .label = MessageId{"storage.setting_play_verbose_wine_log"},
     .reset_group = ResetGroup::Play},
    settings_field<&SettingsValues::play, &PlaySettings::verbose_wine_log>()};

inline constexpr Key<BackendTarget> kBackendTarget{
    {.id = "backend.target",
     .label = MessageId{"storage.setting_backend_target"},
     .sensitivity = Sensitivity::Personal,
     .reset_group = ResetGroup::Backend},
    settings_field<&SettingsValues::backend, &BackendSettings::target>(),
    &BackendTarget::normalize};

inline constexpr Key<bool> kBackendAllowLan{
    {.id = "backend.allow_lan",
     .label = MessageId{"storage.setting_backend_allow_lan"},
     .reset_group = ResetGroup::Backend},
    settings_field<&SettingsValues::backend, &BackendSettings::allow_lan>()};

inline constexpr Key<ConsoleKey> kBackendConsoleKey{
    {.id = "backend.console_key",
     .label = MessageId{"storage.setting_backend_console_key"},
     .reset_group = ResetGroup::Backend},
    settings_field<&SettingsValues::backend, &BackendSettings::console_key>(),
    &validate_console_key};

inline constexpr Key<HostUpdatePolicy> kHostUpdatePolicy{
    {.id = "host.update_policy",
     .label = MessageId{"storage.setting_host_update_policy"},
     .reset_group = ResetGroup::Host},
    settings_field<&SettingsValues::host, &HostSettings::update_policy>()};

// A Host reset also returns every host profile's listing to Unlisted (ResetHooks::reset_records).
inline constexpr Key<HostListing> kHostListing{
    {.id = "host.listing", .label = MessageId{"storage.setting_host_listing"}, .reset_group = ResetGroup::Host},
    settings_field<&SettingsValues::host, &HostSettings::listing>()};

inline constexpr Key<UpdateChannel> kUpdatesChannel{
    {.id = "updates.channel", .label = MessageId{"storage.setting_updates_channel"}},
    settings_field<&SettingsValues::updates, &UpdateSettings::channel>()};

inline constexpr Key<bool> kUpdatesAutoCheck{
    {.id = "updates.auto_check", .label = MessageId{"storage.setting_updates_auto_check"}},
    settings_field<&SettingsValues::updates, &UpdateSettings::auto_check>()};

inline constexpr Key<std::string> kUiLanguage{
    {.id = "ui.language", .label = MessageId{"storage.setting_ui_language"}},
    settings_field<&SettingsValues::ui, &UiSettings::language>(),
    &validate_language_tag};

inline constexpr Key<Theme> kUiTheme{{.id = "ui.theme", .label = MessageId{"storage.setting_ui_theme"}},
                                     settings_field<&SettingsValues::ui, &UiSettings::theme>()};

inline constexpr std::array<const AnyKey*, 14> kAll{
    &kClientGameCulture,  &kPlayCustomArgs,  &kPlayCustomAuthDll, &kPlayEnv,
    &kPlayVerboseWineLog, &kBackendTarget,   &kBackendAllowLan,   &kBackendConsoleKey,
    &kHostUpdatePolicy,   &kHostListing,     &kUpdatesChannel,    &kUpdatesAutoCheck,
    &kUiLanguage,         &kUiTheme};

}  // namespace keys

}  // namespace rb::storage
