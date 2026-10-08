#include "messages.hpp"

namespace reboot::storage::msg {

REBOOT_MESSAGE(kWrongType, "storage.wrong_type", "Expected {expected}");
REBOOT_MESSAGE(kUnknownName, "storage.unknown_name", "{value} is not one of the allowed values");
REBOOT_MESSAGE(kInvalidUtf8, "storage.invalid_utf8", "The stored text is not valid UTF-8");
REBOOT_MESSAGE(kInvalidPath, "storage.invalid_path", "The stored path cannot be decoded");
REBOOT_MESSAGE(kInvalidUuid, "storage.invalid_uuid", "{value} is not a valid id");
REBOOT_MESSAGE(kInvalidVersion, "storage.invalid_version", "{value} is not a valid version");
REBOOT_MESSAGE(kOutOfRange, "storage.out_of_range", "{value} is out of range");
REBOOT_MESSAGE(kInvalidValue, "storage.invalid_value", "{value} is not allowed for {member}");
REBOOT_MESSAGE(kMissingMember, "storage.missing_member", "{member} is missing");
REBOOT_MESSAGE(kReadOnly, "storage.read_only",
               "{document} was saved by a newer version of the launcher (schema {schema}) and is open read-only");
REBOOT_MESSAGE(kMemoryOnly, "storage.memory_only",
               "{document} cannot be loaded from {path}; changes are kept only until the engine exits");
REBOOT_MESSAGE(kCorrupt, "storage.corrupt",
               "{document} and its backup cannot be read; the file was copied to {quarantine} and defaults are used");
REBOOT_MESSAGE(kQuarantineFailed, "storage.quarantine_failed",
               "{document} cannot be read or copied aside to {path}; changes are kept only until the engine exits");
REBOOT_MESSAGE(kRestoredFromBackup, "storage.restored_from_backup",
               "{document} cannot be read, so its backup copy was used");
REBOOT_MESSAGE(kSchemaBackupFailed, "storage.schema_backup_failed",
               "{document} cannot be backed up to {path} before its upgrade, so it is open read-only");
REBOOT_MESSAGE(kUpgradeFailed, "storage.upgrade_failed",
               "{document} cannot be upgraded from schema {schema}, so it is open read-only");
REBOOT_MESSAGE(kWriteFailed, "storage.write_failed", "{document} cannot be saved to {path}");
REBOOT_MESSAGE(kCancelled, "storage.cancelled", "Work on {document} was cancelled");
REBOOT_MESSAGE(kRevisionConflict, "storage.revision_conflict",
               "The settings changed elsewhere (revision {current}, expected {expected})");
REBOOT_MESSAGE(kInvalidSetting, "storage.invalid_setting", "{key} cannot be set to this value");
REBOOT_MESSAGE(kUnknownKey, "storage.unknown_key", "There is no setting named {key}");
REBOOT_MESSAGE(kInvalidConsoleKey, "storage.invalid_console_key", "{key} is not an Unreal Engine key name");
REBOOT_MESSAGE(kInvalidLanguageTag, "storage.invalid_language_tag", "{tag} is neither \"system\" nor a language tag");
REBOOT_MESSAGE(kInvalidLaunchArgs, "storage.invalid_launch_args",
               "The launch arguments contain a control character or an unbalanced quote");
REBOOT_MESSAGE(kInvalidAuthDllPath, "storage.invalid_auth_dll_path", "{path} must be an absolute path to a .dll file");
REBOOT_MESSAGE(kInvalidEnvLine, "storage.invalid_env_line", "Line {line} is not a NAME=value environment variable");
REBOOT_MESSAGE(kInvalidBackendTarget, "storage.invalid_backend_target", "A {kind} backend needs an address");
REBOOT_MESSAGE(kInvalidHost, "storage.invalid_host", "{host} is neither an IP address nor a host name");
REBOOT_MESSAGE(kResetBlocked, "storage.reset_blocked",
               "{sessions} running sessions or the backend still use these settings");
REBOOT_MESSAGE(kResetStopFailed, "storage.reset_stop_failed",
               "The running sessions could not be stopped, so nothing was reset");
REBOOT_MESSAGE(kRootInsidePackage, "storage.root_inside_package",
               "The data folder {root} is inside the app folder {package}, which updates replace");
REBOOT_MESSAGE(kRootNotWritable, "storage.root_not_writable",
               "The data folder {path} cannot be created; changes are kept only until the engine exits");
REBOOT_MESSAGE(kFrontendStateTooLarge, "storage.frontend_state_too_large",
               "The {shell} window state is {size} bytes; the limit is {limit}");
REBOOT_MESSAGE(kFrontendStateNotJson, "storage.frontend_state_not_json", "The {shell} window state is not UTF-8 JSON");
REBOOT_MESSAGE(kInvalidShellName, "storage.invalid_shell_name", "{name} is not a valid shell name");

// Key labels; keys::* names them by id.
REBOOT_MESSAGE(kLabelClientGameCulture, "storage.setting_client_game_culture", "Game language");
REBOOT_MESSAGE(kLabelPlayCustomArgs, "storage.setting_play_custom_args", "Custom launch arguments");
REBOOT_MESSAGE(kLabelPlayCustomAuthDll, "storage.setting_play_custom_auth_dll", "Custom authentication DLL");
REBOOT_MESSAGE(kLabelPlayEnv, "storage.setting_play_env", "Game environment variables");
REBOOT_MESSAGE(kLabelPlayVerboseWineLog, "storage.setting_play_verbose_wine_log", "Verbose Wine logging");
REBOOT_MESSAGE(kLabelBackendTarget, "storage.setting_backend_target", "Backend");
REBOOT_MESSAGE(kLabelBackendAllowLan, "storage.setting_backend_allow_lan", "Allow LAN connections to the backend");
REBOOT_MESSAGE(kLabelBackendConsoleKey, "storage.setting_backend_console_key", "Console key");
REBOOT_MESSAGE(kLabelHostUpdatePolicy, "storage.setting_host_update_policy", "Update a running server");
REBOOT_MESSAGE(kLabelHostListing, "storage.setting_host_listing", "Listed in server browser");
REBOOT_MESSAGE(kLabelUpdatesChannel, "storage.setting_updates_channel", "Update channel");
REBOOT_MESSAGE(kLabelUpdatesAutoCheck, "storage.setting_updates_auto_check", "Check for updates automatically");
REBOOT_MESSAGE(kLabelUiLanguage, "storage.setting_ui_language", "Language");
REBOOT_MESSAGE(kLabelUiTheme, "storage.setting_ui_theme", "Theme");

}  // namespace reboot::storage::msg
