#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::browser {

// Values match rbsb/1; Unknown on an entry is All in a filter.
enum class Region : u8 { All, Africa, Antarctica, Asia, Europe, NorthAmerica, Oceania, SouthAmerica };

// Without keeps the servers that need no password.
enum class PasswordFilter : u8 { Any, Without, Only };

// The three orders rbsb/1 views have; there is no oldest-first or Z-A view.
enum class ServerSort : u8 { Players, Newest, Name };

inline constexpr u32 kSmallWindow = 50;
inline constexpr u32 kLargeWindow = 200;

// Empty `buckets` is every version; several are merged locally. `exact_version` then keeps only
// servers of that build.
struct VersionFilter {
    std::vector<u32> buckets;
    std::optional<GameVersion> exact_version;

    bool operator==(const VersionFilter&) const = default;
};

// Capabilities: server-browser.browse, server-browser.+28, server-browser.+29, server-browser.+65, game-builds.+2.
// The edge lists only online, reachable, listed servers.
struct ViewSpec {
    VersionFilter versions;
    PasswordFilter password = PasswordFilter::Any;
    Region region = Region::All;
    ServerSort sort = ServerSort::Players;
    // Rounded up to 50 or 200 and clamped to Welcome.limits.max_window.
    u32 window = kSmallWindow;

    bool operator==(const ViewSpec&) const = default;

    // Fails with browser.invalid_view_spec: a zero window, or non-empty `buckets` missing one of
    // buckets_for(exact_version), which would hide that build's servers.
    [[nodiscard]] Result<void> validate() const;
};

}  // namespace rb::browser
