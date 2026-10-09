#pragma once

#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"

namespace reboot::identity {

// The endpoint as BackendTarget::normalize leaves it, the key of a BackendLogin. storage.invalid_host.
[[nodiscard]] Result<HostPort> normalize_backend_endpoint(const HostPort& endpoint);

// "host:port", with an IPv6 host in brackets.
[[nodiscard]] std::string endpoint_text(const HostPort& endpoint);

// identity.empty_remote_login for the backend at `endpoint`.
[[nodiscard]] Diagnostic empty_login(const HostPort& endpoint);

}  // namespace reboot::identity
