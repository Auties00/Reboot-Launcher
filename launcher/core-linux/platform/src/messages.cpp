#include "messages.hpp"

namespace reboot::os_linux::platform {

// errno failures use posix::call_failed; a GError's domain and code go in Diagnostic::detail.
REBOOT_MESSAGE(kCallFailed, "platform.call_failed", "{call} failed.");
REBOOT_MESSAGE(kCallFailedOnPath, "platform.call_failed_on_path", "{call} failed on {path}.");
REBOOT_MESSAGE(kNoHome, "platform.no_home", "The home directory of uid {uid} could not be found.");
REBOOT_MESSAGE(kWatchLimit, "platform.watch_limit",
               "The system allows no more file watches; raise fs.inotify.max_user_watches.");
REBOOT_MESSAGE(kSecretServiceTimeout, "platform.secret_service_timeout",
               "The keyring did not answer within {deadline}; it may be locked.");
REBOOT_MESSAGE(kSecretUnreadable, "platform.secret_unreadable", "The stored secret {key} could not be read.");
REBOOT_MESSAGE(kUrlNotHttps, "platform.url_not_https", "Only https links can be opened.");
REBOOT_MESSAGE(kNoOpener, "platform.no_opener", "No desktop application could open {target}.");
REBOOT_MESSAGE(kTrashUnavailable, "platform.trash_unavailable", "{path} cannot be moved to the trash.");
REBOOT_MESSAGE(kIntegrationForeign, "platform.integration_foreign", "Another application owns {entry}.");
REBOOT_MESSAGE(kIntegrationNeedsUserInstall, "platform.integration_needs_user_install",
               "Only the AppImage or the self-installed copy of Reboot Launcher can register {entry}.");
REBOOT_MESSAGE(kSystemdUnavailable, "platform.systemd_unavailable", "The systemd user manager is not running.");
REBOOT_MESSAGE(kEngineAgentDefaultRootOnly, "platform.engine_agent_default_root_only",
               "The engine service can only be registered for the default data folder.");
REBOOT_MESSAGE(kHelperFailed, "platform.helper_failed", "{program} exited with status {exit_code}.");
REBOOT_MESSAGE(kPython3Missing, "platform.python3_missing", "Install Python 3 to play through umu.");
REBOOT_MESSAGE(kVulkanLoaderMissing, "platform.vulkan_loader_missing",
               "Install the Vulkan loader (libvulkan1 or vulkan-loader) and your GPU's Vulkan driver.");
REBOOT_MESSAGE(kLingerDisabled, "platform.linger_disabled",
               "Hosted servers stop when you log out unless lingering is enabled for your user.");
REBOOT_MESSAGE(kNoRemediation, "platform.no_remediation", "{id} cannot be fixed automatically.");
REBOOT_MESSAGE(kUpdateNotifyOnly, "platform.update_notify_only",
               "This copy of Reboot Launcher is updated by its container or package, not by itself.");
REBOOT_MESSAGE(kUpdatePackageInvalid, "platform.update_package_invalid",
               "The update package {path} does not hold exactly one version directory.");
REBOOT_MESSAGE(kUpdateEntryUnsafe, "platform.update_entry_unsafe",
               "The update package {path} holds an unsafe entry {entry}.");
REBOOT_MESSAGE(kUpdateNotStaged, "platform.update_not_staged", "No update has been staged.");
REBOOT_MESSAGE(kLockBusy, "platform.lock_busy", "{path} is locked by another process.");
REBOOT_MESSAGE(kHelperTimeout, "platform.helper_timeout", "{program} did not finish within {deadline}.");
REBOOT_MESSAGE(kDnsFailed, "platform.dns_failed", "{host} could not be resolved.");
REBOOT_MESSAGE(kDnsCancelled, "platform.dns_cancelled", "Resolving {host} was cancelled.");

}  // namespace reboot::os_linux::platform
