#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/net/url_scheme.hpp"
#include "reboot/storage/backend_target.hpp"

namespace rb::backend {

inline constexpr Port kDefaultBackendPort = storage::kDefaultBackendPort;

// Without a scheme, https is tried before http.
struct BackendUrl {
    std::optional<net::UrlScheme> scheme;
    std::string host;
    Port port = kDefaultBackendPort;

    bool operator==(const BackendUrl&) const = default;

    // "host", "host:port", "[v6]:port", each with an optional http:// or https:// prefix and
    // trailing "/". A path, query or user info fails with backend.invalid_url; port 0 with
    // backend.invalid_port. The host is lowercased.
    [[nodiscard]] static Result<BackendUrl> parse(std::string_view text);

    // "scheme://host:port" with no trailing slash; https when no scheme is set.
    [[nodiscard]] std::string origin() const;
    [[nodiscard]] HostPort endpoint() const { return HostPort{host, port}; }
};

}  // namespace rb::backend
