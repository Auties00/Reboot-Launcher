#pragma once

#include <span>

#include "reboot/browser/view_spec.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::browser {

// Installed is 10.0.9's "playable" filter: one bucket per installed build.
enum class VersionScope : u8 { All, Installed };

// Capabilities: server-browser.browse.
// The filter and sort a user last picked, persisted so every UI opens its list with them.
struct BrowseChoices {
    VersionScope versions = VersionScope::All;
    PasswordFilter password = PasswordFilter::Any;
    Region region = Region::All;
    ServerSort sort = ServerSort::Players;

    bool operator==(const BrowseChoices&) const = default;
};

// Installed takes buckets_for of every version in `installed`, and every version when none is.
[[nodiscard]] ViewSpec make_view_spec(const BrowseChoices& choices, std::span<const GameVersion> installed,
                                      u32 window);

}  // namespace reboot::browser
