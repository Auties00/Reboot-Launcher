#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/front/upstream_origin.hpp"

namespace rb::front {

// What the game's URLs look like for one upstream: the front origin, plus the path prefix that reaches it.
struct FrontBase {
    // "http://127.0.0.1:<listener port>".
    std::string origin;
    // "/s/<key>", with the relay segment for a relayed upstream; empty on a fixed listener.
    std::string prefix;
};

// The scheme's default port is left out, as clients write it.
[[nodiscard]] std::string host_header(const UpstreamOrigin& origin);

// "/@/<ws|wss>/<host:port>", which parse_front_path reads back.
[[nodiscard]] std::string relay_segment(const UpstreamOrigin& origin);

// X-Reboot-*, in any case.
[[nodiscard]] bool is_reboot_header(std::string_view name) noexcept;

// A Location naming `upstream`, or path-absolute, mapped under `base`; nullopt leaves it unchanged.
[[nodiscard]] std::optional<std::string> rewrite_location(std::string_view location, const UpstreamOrigin& upstream,
                                                          const FrontBase& base);

}  // namespace rb::front
