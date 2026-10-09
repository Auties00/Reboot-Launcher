#include "messages.hpp"

namespace rb::os_windows::platform {

// Win32 and HRESULT failures; the code travels as the diagnostic's os_error.
REBOOT_MESSAGE(kCallFailed, "platform.call_failed", "{call} failed.");
REBOOT_MESSAGE(kCallFailedOnPath, "platform.call_failed_on_path", "{call} failed on {path}.");
REBOOT_MESSAGE(kNotSupported, "platform.not_supported", "This is not available on this system.");
REBOOT_MESSAGE(kNoRemediation, "platform.no_remediation", "{id} cannot be fixed automatically.");
REBOOT_MESSAGE(kUrlNotHttps, "platform.url_not_https", "Only https links can be opened.");
REBOOT_MESSAGE(kSecretUnreadable, "platform.secret_unreadable", "The stored secret {key} could not be read.");
REBOOT_MESSAGE(kTrashUnavailable, "platform.trash_unavailable", "{path} cannot be moved to the trash.");
REBOOT_MESSAGE(kLockBusy, "platform.lock_busy", "{path} is locked by another process.");
REBOOT_MESSAGE(kFileVanished, "platform.file_vanished",
               "{path} disappeared after it was checked; security software may have quarantined it.");
REBOOT_MESSAGE(kPayloadHashMismatch, "platform.payload_hash_mismatch", "{path} does not match its expected checksum.");
REBOOT_MESSAGE(kSecretTooLarge, "platform.secret_too_large",
               "The secret is {size} bytes, but Credential Manager holds at most {limit}.");
REBOOT_MESSAGE(kSessionStuck, "platform.session_stuck",
               "{count} processes of the game session were still running after it was stopped.");
REBOOT_MESSAGE(kWmiTimeout, "platform.wmi_timeout", "Windows Security Center did not answer within {deadline}.");
REBOOT_MESSAGE(kWindowsTooOld, "platform.windows_too_old", "Reboot Launcher needs Windows 10 version 1809 or later.");
REBOOT_MESSAGE(kUpdateNotSupported, "platform.update_not_supported", "This portable copy cannot update itself.");
REBOOT_MESSAGE(kVelopackStageFailed, "platform.velopack_stage_failed",
               "The downloaded update could not be handed to the updater.");
REBOOT_MESSAGE(kVelopackApplyFailed, "platform.velopack_apply_failed",
               "The updater could not be started to apply the update.");

}  // namespace rb::os_windows::platform
