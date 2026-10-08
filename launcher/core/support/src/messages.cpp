#include "messages.hpp"

namespace reboot::support::msg {

REBOOT_MESSAGE(kVersionUnknown, "support.version_unknown",
               "The game version of this build is not known yet. Choose it before you play or host");
REBOOT_MESSAGE(kAboveVersionCap, "support.above_version_cap",
               "Fortnite {version} is newer than {cap}, the newest version the launcher supports");
REBOOT_MESSAGE(kAboveVersionCapOptInRequired, "support.above_version_cap_opt_in_required",
               "Fortnite {version} is newer than {cap}. Allow unsupported versions for this build to use it anyway");
REBOOT_MESSAGE(kAboveVersionCapOptedIn, "support.above_version_cap_opted_in",
               "Fortnite {version} is newer than {cap} and runs only because unsupported versions are allowed for "
               "this build");
REBOOT_MESSAGE(kRunnerUnavailable, "support.runner_unavailable",
               "The selected runtime is not available on this computer");
REBOOT_MESSAGE(kHostNeedsNativeRunner, "support.host_needs_native_runner",
               "Hosting runs the game server directly and cannot use a Wine runtime");
REBOOT_MESSAGE(kGameServerUnavailable, "support.game_server_unavailable",
               "The game server has not reported which versions it supports yet");
REBOOT_MESSAGE(kNotCoveredByGameServer, "support.not_covered_by_game_server",
               "The game server cannot host Fortnite {version}");
REBOOT_MESSAGE(kCustomAuthDll, "support.custom_auth_dll",
               "A custom authentication DLL replaces the launcher's own, so this setup is untested");
REBOOT_MESSAGE(kExternalBackend, "support.external_backend",
               "This setup uses a backend other than the launcher's own, so it is untested");
REBOOT_MESSAGE(kNoEvidence, "support.no_evidence", "Fortnite {version} has not been tested in this setup");
REBOOT_MESSAGE(kEvidenceStale, "support.evidence_stale",
               "Fortnite {version} was tested only with other versions of the launcher's components");
REBOOT_MESSAGE(kEvidenceFailed, "support.evidence_failed", "Fortnite {version} failed its latest test in this setup");
REBOOT_MESSAGE(kMalformedServerDescription, "support.malformed_server_description",
               "The game server reported an invalid version range, {min} to {max}");
REBOOT_MESSAGE(kMatrixReportUnknownSchema, "support.matrix_report_unknown_schema",
               "The test report uses schema {schema}, which this version cannot read");
REBOOT_MESSAGE(kMatrixReportMalformed, "support.matrix_report_malformed", "The test report is malformed at {where}");

}  // namespace reboot::support::msg
