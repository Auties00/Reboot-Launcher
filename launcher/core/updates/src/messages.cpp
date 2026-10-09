#include "messages.hpp"

namespace reboot::updates::msg {

REBOOT_MESSAGE(kNotifyOnly, "updates.notify_only",
               "This installation is updated by its package manager; install version {version} there");
REBOOT_MESSAGE(kNoUpdate, "updates.no_update", "There is no launcher update to apply");
REBOOT_MESSAGE(kBusy, "updates.busy", "A launcher update is already in progress");
REBOOT_MESSAGE(kBelowMinSupported, "updates.below_min_supported",
               "Launcher {installed} is no longer supported; update to {min_supported} or later to start new sessions");
REBOOT_MESSAGE(kCheckFailed, "updates.check_failed", "Cannot check for launcher updates");
REBOOT_MESSAGE(kDownloadFailed, "updates.download_failed", "Cannot download launcher {version}");
REBOOT_MESSAGE(kChecksumMismatch, "updates.checksum_mismatch",
               "The download of launcher {version} does not match its published checksum");
REBOOT_MESSAGE(kStageFailed, "updates.stage_failed", "Cannot prepare launcher {version} for installation");
REBOOT_MESSAGE(kApplyFailed, "updates.apply_failed", "Cannot install launcher {version}");
REBOOT_MESSAGE(kNotApplied, "updates.not_applied",
               "Launcher {version} was not installed; {installed} is still running");
REBOOT_MESSAGE(kSelfTestFailed, "updates.self_test_failed",
               "Launcher {version} failed its startup check (attempt {attempt} of {max_attempts})");
REBOOT_MESSAGE(kGaveUp, "updates.gave_up", "Launcher {version} failed its startup check {attempts} times");
REBOOT_MESSAGE(kStopDeclined, "updates.stop_declined", "The update will install once running sessions end");
REBOOT_MESSAGE(kAnswerInvalid, "updates.answer_invalid", "That answer does not settle the question");
REBOOT_MESSAGE(kMarkerMalformed, "updates.marker_malformed", "The update marker has a malformed {field}");

}  // namespace reboot::updates::msg
