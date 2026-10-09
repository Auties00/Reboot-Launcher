#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::browser {

inline constexpr std::string_view kDeepLinkScheme = "reboot";

struct DeepLink {
    ServerId server;

    bool operator==(const DeepLink&) const = default;
};

// Capabilities: server-browser.+38.
// reboot://<uuid> in any letter case (hosts may lowercase the scheme), with an optional trailing
// slash and surrounding quotes, or a bare server id; anything else fails with browser.invalid_link.
[[nodiscard]] Result<DeepLink> parse_deep_link(std::string_view text);

}  // namespace rb::browser
