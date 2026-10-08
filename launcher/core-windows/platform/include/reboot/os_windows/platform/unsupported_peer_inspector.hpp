#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; Windows exposes no loopback socket owner uid, so this always fails
// with platform.not_supported.
class UnsupportedPeerInspector final : public ports::ILoopbackPeerInspector {
public:
    Result<std::optional<u32>> peer_uid(Endpoint local, Endpoint remote) override;
};

}  // namespace reboot::os_windows::platform
