#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/front/session_key.hpp"
#include "reboot/front/upstream_origin.hpp"

namespace rb::front {

// A request target on the session listener, split into its route and the upstream path.
struct FrontPath {
    SessionKey key;
    // From /s/<key>/@/<ws|wss>/<authority>/..., the form ClientDllConfig::ws_rewrite produces.
    std::optional<UpstreamOrigin> relay;
    // Starts with '/' and keeps the query.
    std::string rest;

    bool operator==(const FrontPath&) const = default;
};

// nullopt for anything not under /s/<32 hex digits>, or a malformed relay segment.
[[nodiscard]] std::optional<FrontPath> parse_front_path(std::string_view target);

}  // namespace rb::front
