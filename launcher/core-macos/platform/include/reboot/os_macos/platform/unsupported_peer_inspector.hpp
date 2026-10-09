#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace rb::os_macos::platform {

// Covers no capability ids; credential-security requires the loopback peer check on Linux only.
class UnsupportedPeerInspector final : public ports::ILoopbackPeerInspector {
public:
    Result<std::optional<u32>> peer_uid(Endpoint local, Endpoint remote) override;
};

}  // namespace rb::os_macos::platform
