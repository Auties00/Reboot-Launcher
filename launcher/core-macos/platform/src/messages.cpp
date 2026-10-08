#include "messages.hpp"

namespace reboot::os_macos::platform {

// errno failures use posix::call_failed; these carry an OSStatus or NSError code.
REBOOT_MESSAGE(kCallFailed, "platform.call_failed", "{call} failed.");
REBOOT_MESSAGE(kCallFailedOnPath, "platform.call_failed_on_path", "{call} failed on {path}.");
REBOOT_MESSAGE(kNotSupported, "platform.not_supported", "This is not available on macOS.");
REBOOT_MESSAGE(kKeychainLocked, "platform.keychain_locked", "The login keychain is locked.");
REBOOT_MESSAGE(kNoGuiSession, "platform.no_gui_session",
               "Opening links, folders and Finder windows needs a logged-in desktop session.");
REBOOT_MESSAGE(kUrlNotHttps, "platform.url_not_https", "Only https links can be opened.");
REBOOT_MESSAGE(kNotInBundle, "platform.not_in_bundle", "{path} is not inside the Reboot Launcher app.");
REBOOT_MESSAGE(kAppTranslocated, "platform.app_translocated",
               "Reboot Launcher runs from a temporary copy. Move it to the Applications folder and open it again.");
REBOOT_MESSAGE(kIntegrationForeign, "platform.integration_foreign", "Another application owns {entry}.");
REBOOT_MESSAGE(kAgentRequiresApproval, "platform.agent_requires_approval",
               "Allow Reboot Launcher in System Settings under General, Login Items.");
REBOOT_MESSAGE(kNoRemediation, "platform.no_remediation", "{id} cannot be fixed automatically.");
REBOOT_MESSAGE(kNeedsAppleSilicon, "platform.mac_needs_apple_silicon",
               "Playing on macOS needs a Mac with Apple silicon.");
REBOOT_MESSAGE(kMacosTooOld, "platform.macos_too_old", "Reboot Launcher needs macOS {minimum_version} or later.");
REBOOT_MESSAGE(kRosettaMissing, "platform.rosetta_missing", "Playing needs Rosetta 2, which is not installed.");
REBOOT_MESSAGE(kMetal3Missing, "platform.metal3_missing", "Playing needs a graphics card that supports Metal 3.");
REBOOT_MESSAGE(kRosettaInstallFailed, "platform.rosetta_install_failed",
               "Installing Rosetta 2 failed with exit code {exit_code}.");
REBOOT_MESSAGE(kFirewallBlocksGameServer, "platform.firewall_blocks_game_server",
               "The macOS firewall blocks incoming connections to the game server.");
REBOOT_MESSAGE(kLocalNetworkDenied, "platform.local_network_denied",
               "Local Network access is turned off for Reboot Launcher.");
REBOOT_MESSAGE(kUpdateOutsideBundle, "platform.update_outside_bundle",
               "This copy does not run from an installed app, so it cannot update itself.");
REBOOT_MESSAGE(kVelopackStageFailed, "platform.velopack_stage_failed",
               "The downloaded update could not be handed to the updater.");
REBOOT_MESSAGE(kVelopackApplyFailed, "platform.velopack_apply_failed",
               "The updater could not be started to apply the update.");
REBOOT_MESSAGE(kUpdateSwapTimeout, "platform.update_swap_timeout", "The updater did not finish within {deadline}.");
REBOOT_MESSAGE(kUpdateNotApplied, "platform.update_not_applied",
               "The updater finished without installing version {version}.");

}  // namespace reboot::os_macos::platform
