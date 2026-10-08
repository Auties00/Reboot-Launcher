#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/net_types.hpp"
#include "reboot/front/upstream_origin.hpp"
#include "reboot/net/url_scheme.hpp"

namespace reboot::front {

struct Authority {
    std::string_view host;
    std::optional<Port> port;
};

// host, host:port, [v6] or [v6]:port; nullopt for an empty host or a bad port.
[[nodiscard]] std::optional<Authority> split_authority(std::string_view text);

// Lowercase without a trailing dot, an IP literal in canonical form; nullopt when neither IP nor DNS name.
[[nodiscard]] std::optional<std::string> normalize_host(std::string_view host);

[[nodiscard]] std::optional<UpstreamOrigin> origin_from_authority(net::UrlScheme scheme, std::string_view authority);

// localhost, *.localhost, 127.0.0.0/8, ::1 and the unspecified addresses, which also reach this machine.
[[nodiscard]] bool is_loopback_host(std::string_view normalized_host);

[[nodiscard]] bool reaches_this_machine(const IpAddress& address) noexcept;

}  // namespace reboot::front
