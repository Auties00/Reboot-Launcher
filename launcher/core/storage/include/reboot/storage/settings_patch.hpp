#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/backend_target.hpp"
#include "reboot/storage/console_key.hpp"
#include "reboot/storage/settings_values.hpp"

namespace rb::storage {

// Mirrors SettingsValues; an empty field leaves that setting alone.
struct SettingsPatch {
    // Refused with storage.revision_conflict unless it equals the current revision.
    std::optional<u64> expected_revision;

    struct Client {
        std::optional<std::string> game_culture;
    } client;

    struct Play {
        std::optional<std::string> custom_args;
        // An engaged empty inner value clears the setting.
        std::optional<std::optional<NativePath>> custom_auth_dll;
        std::optional<std::string> env;
        std::optional<bool> verbose_wine_log;
    } play;

    struct Backend {
        std::optional<BackendTarget> target;
        std::optional<bool> allow_lan;
        std::optional<ConsoleKey> console_key;
    } backend;

    struct Host {
        std::optional<HostUpdatePolicy> update_policy;
        std::optional<HostListing> listing;
    } host;

    struct Updates {
        std::optional<UpdateChannel> channel;
        std::optional<bool> auto_check;
    } updates;

    struct Ui {
        std::optional<std::string> language;
        std::optional<Theme> theme;
    } ui;
};

}  // namespace rb::storage
