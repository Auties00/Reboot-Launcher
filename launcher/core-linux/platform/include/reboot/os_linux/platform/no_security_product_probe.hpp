#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_linux::platform {

// Covers no capability ids; Linux has no security center to query, so probe always returns nullopt.
class NoSecurityProductProbe final : public ports::ISecurityProductProbe {
public:
    Result<std::optional<ports::SecurityProducts>> probe() override { return std::nullopt; }
};

}  // namespace reboot::os_linux::platform
