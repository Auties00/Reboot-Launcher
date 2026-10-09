#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/net_types.hpp"
#include "reboot/net/url_scheme.hpp"

namespace rb::net {

// An absolute http or https URL. `host` is lowercase and has no IPv6 brackets.
struct ParsedUrl {
    UrlScheme scheme = UrlScheme::Https;
    std::string host;
    std::optional<Port> port;
};

// Rejects user info, so credentials never travel in a URL that may be logged, and anything with
// whitespace or control characters.
[[nodiscard]] std::optional<ParsedUrl> parse_url(std::string_view url);

// The host of `url` for diagnostics, or the whole text when it does not parse.
[[nodiscard]] std::string host_for_diagnostic(std::string_view url);

// Lowercase, without brackets or a trailing ":port".
[[nodiscard]] std::string normalize_host(std::string_view host);

}  // namespace rb::net
