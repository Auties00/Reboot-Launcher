#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "reboot/backend/backend_url.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/identity/login_target.hpp"

namespace reboot::backend {

inline constexpr std::string_view kBackendInfoPath = "/reboot/v1/backend-info";

// What RemoteBackendProbe learnt about an upstream; the optional fields are set for Reboot only.
struct BackendInfo {
    // The scheme that answered is always set.
    BackendUrl url;
    identity::UpstreamFlavor flavor = identity::UpstreamFlavor::ThirdParty;
    std::optional<std::string> version;
    std::optional<u32> api_version;
    // The upstream's XMPP and matchmaker WebSocket listener, which the front relays to.
    std::optional<Port> ws_port;

    bool operator==(const BackendInfo&) const = default;
};

}  // namespace reboot::backend
