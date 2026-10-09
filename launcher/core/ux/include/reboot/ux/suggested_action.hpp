#pragma once

#include <string>
#include <variant>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ux/app_links.hpp"

namespace rb::ux {

// Each alternative names the API calls the UI makes when the user picks it; core never makes them.

// Integration.remediate(prerequisite).
struct RemediatePrerequisite {
    std::string prerequisite_id;
};

// Catalog.list, then Install.install.
struct InstallBuild {};

// Library.import.
struct ImportBuild {};

// Identity.set_display_name(role).
struct EditDisplayName {
    contracts::backend::AccountRole role{};
};

// Settings.patch of host.listing, then Host.profiles_update of the built-in default profile's listing.
struct SetDefaultHostListing {
    bool listed = false;
};

// Host.profiles_update of `profile` with listing Listed; HostService pushes it to the profile's live sessions.
struct ListHostProfile {
    HostProfileId profile;
};

// Host.share_link(session), copied by the UI.
struct CopyShareLink {
    SessionId session;
};

// Integration shell.open_url(AppLinks::url(link, language)).
struct OpenAppLink {
    AppLink link{};
};

// Capabilities: onboarding-ux-flows.first-run-tour.
using SuggestedAction = std::variant<RemediatePrerequisite, InstallBuild, ImportBuild, EditDisplayName,
                                     SetDefaultHostListing, ListHostProfile, CopyShareLink, OpenAppLink>;

[[nodiscard]] MessageId action_label(const SuggestedAction& action);

}  // namespace rb::ux
