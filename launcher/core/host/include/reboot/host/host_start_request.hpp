#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/host/host_profile.hpp"
#include "reboot/sessions/lease.hpp"

namespace reboot::host {

// Apply to this start only; the profile is not changed. --listed/--unlisted and --port in the CLI.
struct HostOverrides {
    std::optional<HostListing> listing;
    // Pins the block at this port for this start.
    std::optional<Port> port;
    std::optional<BuildId> build;
};

struct HostStartRequest {
    HostProfileId profile;
    HostOverrides overrides;
    sessions::Lease lease = sessions::Lease::engine();
    DisconnectPolicy disconnect = DisconnectPolicy::Detached;
    // The play session a linked auto-server belongs to; set exactly when the profile is the auto
    // profile. The server stops with it and stays its child across restarts.
    std::optional<SessionId> linked_to;
    // Consent given up front, so a headless caller is never parked on a request; otherwise an
    // Untested build raises ConfirmUntested.
    bool untested_confirmed = false;
};

// The start rules that need only the request and its profile: linked_to with the auto profile and
// only with it (host.linked_needs_auto_profile, host.auto_profile_needs_link), no Listed override
// of the auto profile (host.auto_profile_listed), and an override port that passes validate().
[[nodiscard]] Result<void> check_start(const HostStartRequest& request, const HostProfile& profile);

}  // namespace reboot::host
