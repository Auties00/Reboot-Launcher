#pragma once

#include "reboot/storage/settings_values.hpp"

namespace rb::publish {

// Capabilities: hosting.+19, hosting.+64, hosting.+80.
// storage owns the persisted names; Unlisted entries stay joinable by link or id through Resolve.
using Listing = storage::HostListing;

// Recomputed for every HostRegister and HostUpdate, so nothing has to be restored after a restart.
[[nodiscard]] constexpr bool hidden_on_edge(Listing listing, bool restarting) noexcept {
    return listing == Listing::Unlisted || restarting;
}

}  // namespace rb::publish
