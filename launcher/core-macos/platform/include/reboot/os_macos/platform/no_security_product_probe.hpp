#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/os_services.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; macOS cannot enumerate security products, so probe returns nullopt.
class NoSecurityProductProbe final : public ports::ISecurityProductProbe {
public:
    Result<std::optional<ports::SecurityProducts>> probe() override { return std::nullopt; }
};

}  // namespace reboot::os_macos::platform
