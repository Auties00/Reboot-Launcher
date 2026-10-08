#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; ISecurityProductProbe for remediation that names the AV product.
class WmiSecurityProductProbe final : public ports::ISecurityProductProbe {
public:
    // A query past the 5 s deadline abandons its MTA thread, which owns all its COM state and
    // touches nothing of the probe, so the probe may be destroyed first.
    Result<std::optional<ports::SecurityProducts>> probe() override;
};

}  // namespace reboot::os_windows::platform
