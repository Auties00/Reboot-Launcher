#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::integration {

enum class Remedy : u8 {
    // Guidance only, such as installing a driver or a distro package.
    None,
    // Opens the OS settings pane (firewall, Local Network).
    OpenSettings,
    // loginctl enable-linger.
    Enable,
    // Installs an OS component whose licence the UI shows before asking (Rosetta).
    Install,
};

}  // namespace reboot::integration
