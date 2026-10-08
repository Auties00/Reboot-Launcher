#pragma once

#include "reboot/foundation/diag.hpp"

namespace reboot::ux::msg {

REBOOT_MESSAGE_DECL(kMalformedLanguageTag);
REBOOT_MESSAGE_DECL(kMalformedCatalog);
REBOOT_MESSAGE_DECL(kOnboardingNotInProgress);
REBOOT_MESSAGE_DECL(kOnboardingNotCurrentStep);
REBOOT_MESSAGE_DECL(kOnboardingChoiceRequired);
REBOOT_MESSAGE_DECL(kOnboardingChoiceNotOffered);
REBOOT_MESSAGE_DECL(kUnknownNotice);
REBOOT_MESSAGE_DECL(kNoticeNotDismissible);

REBOOT_MESSAGE_DECL(kStepWelcomeTitle);
REBOOT_MESSAGE_DECL(kStepWelcomeBody);
REBOOT_MESSAGE_DECL(kStepPrerequisitesTitle);
REBOOT_MESSAGE_DECL(kStepPrerequisitesBody);
REBOOT_MESSAGE_DECL(kStepProfileTitle);
REBOOT_MESSAGE_DECL(kStepProfileBody);
REBOOT_MESSAGE_DECL(kStepLibraryTitle);
REBOOT_MESSAGE_DECL(kStepLibraryBody);
REBOOT_MESSAGE_DECL(kStepPlayTitle);
REBOOT_MESSAGE_DECL(kStepPlayBody);
REBOOT_MESSAGE_DECL(kStepBrowserTitle);
REBOOT_MESSAGE_DECL(kStepBrowserBody);
REBOOT_MESSAGE_DECL(kStepHostListingTitle);
REBOOT_MESSAGE_DECL(kStepHostListingBody);
REBOOT_MESSAGE_DECL(kStepBackendTitle);
REBOOT_MESSAGE_DECL(kStepBackendBody);
REBOOT_MESSAGE_DECL(kStepFinishTitle);
REBOOT_MESSAGE_DECL(kStepFinishBody);
REBOOT_MESSAGE_DECL(kChoiceListPublicly);
REBOOT_MESSAGE_DECL(kChoiceKeepUnlisted);

REBOOT_MESSAGE_DECL(kNoticeUnlistedLiveTitle);
REBOOT_MESSAGE_DECL(kNoticeUnlistedLiveBody);

REBOOT_MESSAGE_DECL(kActionRemediatePrerequisite);
REBOOT_MESSAGE_DECL(kActionInstallBuild);
REBOOT_MESSAGE_DECL(kActionImportBuild);
REBOOT_MESSAGE_DECL(kActionEditDisplayName);
REBOOT_MESSAGE_DECL(kActionListHostProfile);
REBOOT_MESSAGE_DECL(kActionCopyShareLink);

REBOOT_MESSAGE_DECL(kLinkBugReport);
REBOOT_MESSAGE_DECL(kLinkReleases);
REBOOT_MESSAGE_DECL(kLinkDiscord);
REBOOT_MESSAGE_DECL(kLinkPortForwardingGuide);

REBOOT_MESSAGE_DECL(kHelpPortForwardingPort);
REBOOT_MESSAGE_DECL(kHelpPortForwardingPortRange);

}  // namespace reboot::ux::msg
