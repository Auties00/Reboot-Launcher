#include "messages.hpp"

namespace reboot::ux::msg {

REBOOT_MESSAGE(kMalformedLanguageTag, "ux.malformed_language_tag", "{value} is not a valid language tag");
REBOOT_MESSAGE(kMalformedCatalog, "ux.malformed_catalog", "The {language} message catalog is malformed");
REBOOT_MESSAGE(kOnboardingNotInProgress, "ux.onboarding_not_in_progress", "The tour is not running");
REBOOT_MESSAGE(kOnboardingNotCurrentStep, "ux.onboarding_not_current_step", "Tour step {step} is not the current step");
REBOOT_MESSAGE(kOnboardingChoiceRequired, "ux.onboarding_choice_required",
               "Tour step {step} needs an answer before it can continue");
REBOOT_MESSAGE(kOnboardingChoiceNotOffered, "ux.onboarding_choice_not_offered",
               "Tour step {step} does not offer the answer {choice}");
REBOOT_MESSAGE(kUnknownNotice, "ux.unknown_notice", "There is no {notice} notice");
REBOOT_MESSAGE(kNoticeNotDismissible, "ux.notice_not_dismissible", "The {notice} notice cannot be dismissed");

REBOOT_MESSAGE(kStepWelcomeTitle, "ux.step_welcome_title", "Welcome to Reboot Launcher");
REBOOT_MESSAGE(kStepWelcomeBody, "ux.step_welcome_body",
               "Play and host old Fortnite builds with the community. This tour shows where everything is. "
               "You can skip any step or leave the tour at any time.");
REBOOT_MESSAGE(kStepPrerequisitesTitle, "ux.step_prerequisites_title", "Finish setting up this computer");
REBOOT_MESSAGE(kStepPrerequisitesBody, "ux.step_prerequisites_body",
               "Some things the launcher needs are missing. Fix them now, or later from Settings.");
REBOOT_MESSAGE(kStepProfileTitle, "ux.step_profile_title", "Choose your player name");
REBOOT_MESSAGE(kStepProfileBody, "ux.step_profile_body",
               "Other players see this name in the lobby and in the killfeed. A new name applies from the next "
               "launch.");
REBOOT_MESSAGE(kStepLibraryTitle, "ux.step_library_title", "Add a Fortnite build");
REBOOT_MESSAGE(kStepLibraryBody, "ux.step_library_body",
               "Download a build from the catalog, or import one you already have.");
REBOOT_MESSAGE(kStepPlayTitle, "ux.step_play_title", "Play");
REBOOT_MESSAGE(kStepPlayBody, "ux.step_play_body", "Pick a build and a server, then start the game.");
REBOOT_MESSAGE(kStepBrowserTitle, "ux.step_browser_title", "Server browser");
REBOOT_MESSAGE(kStepBrowserBody, "ux.step_browser_body", "Find servers hosted by other players and join them.");
REBOOT_MESSAGE(kStepHostListingTitle, "ux.step_host_listing_title",
               "Should the servers you host appear in the server browser?");
REBOOT_MESSAGE(kStepHostListingBody, "ux.step_host_listing_body",
               "Anyone can join a listed server that has no password. Unlisted servers stay out of the server "
               "browser, but anyone with the link or server id can still join them. Every player who joins, "
               "listed or not, receives your public IP address. You can change this in Settings.");
REBOOT_MESSAGE(kStepBackendTitle, "ux.step_backend_title", "Backend");
REBOOT_MESSAGE(kStepBackendBody, "ux.step_backend_body",
               "The launcher runs its own backend for your games. You can point it at another backend in Settings.");
REBOOT_MESSAGE(kStepFinishTitle, "ux.step_finish_title", "You are ready");
REBOOT_MESSAGE(kStepFinishBody, "ux.step_finish_body", "You can restart this tour from Settings at any time.");
REBOOT_MESSAGE(kChoiceListPublicly, "ux.choice_list_publicly", "List publicly");
REBOOT_MESSAGE(kChoiceKeepUnlisted, "ux.choice_keep_unlisted", "Keep unlisted");

REBOOT_MESSAGE(kNoticeUnlistedLiveTitle, "ux.notice_unlisted_live_title",
               "Unlisted: only people with your link can join");
REBOOT_MESSAGE(kNoticeUnlistedLiveBody, "ux.notice_unlisted_live_body",
               "Your server is not in the server browser. Anyone with the link or server id can still join.");

REBOOT_MESSAGE(kActionRemediatePrerequisite, "ux.action_remediate_prerequisite", "Fix");
REBOOT_MESSAGE(kActionInstallBuild, "ux.action_install_build", "Download a build");
REBOOT_MESSAGE(kActionImportBuild, "ux.action_import_build", "Import a build");
REBOOT_MESSAGE(kActionEditDisplayName, "ux.action_edit_display_name", "Change name");
REBOOT_MESSAGE(kActionListHostProfile, "ux.action_list_host_profile", "List publicly");
REBOOT_MESSAGE(kActionCopyShareLink, "ux.action_copy_share_link", "Copy link");

REBOOT_MESSAGE(kLinkBugReport, "ux.link_bug_report", "Report a bug");
REBOOT_MESSAGE(kLinkReleases, "ux.link_releases", "Release notes");
REBOOT_MESSAGE(kLinkDiscord, "ux.link_discord", "Discord");
REBOOT_MESSAGE(kLinkPortForwardingGuide, "ux.link_port_forwarding_guide", "Port forwarding and VPN guide");

REBOOT_MESSAGE(kHelpPortForwardingPort, "ux.help_port_forwarding_port",
               "Other players cannot reach your server. Forward UDP port {port} on your router to this computer, "
               "or use a VPN such as Playit.");
REBOOT_MESSAGE(kHelpPortForwardingPortRange, "ux.help_port_forwarding_port_range",
               "Other players cannot reach your server. Forward UDP ports {first_port} to {last_port} on your "
               "router to this computer, or use a VPN such as Playit.");

}  // namespace reboot::ux::msg
