#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/net/url_scheme.hpp"

namespace reboot::front {

// Where the front connects; ws and wss parse as Http and Https.
struct UpstreamOrigin {
    net::UrlScheme scheme = net::UrlScheme::Http;
    // Lowercase without a trailing dot; an IP literal in canonical form, IPv6 without brackets.
    std::string host;
    Port port;

    bool operator==(const UpstreamOrigin&) const = default;

    // "https://host:port", the form ConfirmUnencryptedUpstream shows.
    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] constexpr Port default_port(net::UrlScheme scheme) noexcept {
    return scheme == net::UrlScheme::Https ? Port{443} : Port{80};
}

// http, https, ws or wss; the port defaults by scheme and any path is ignored. Fails with front.upstream_invalid.
[[nodiscard]] Result<UpstreamOrigin> parse_upstream_origin(std::string_view url);

}  // namespace reboot::front
