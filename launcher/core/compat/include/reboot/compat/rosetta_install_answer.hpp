#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::compat {

// Installed is checked again; while Rosetta is still missing the request stays pending.
enum class RosettaInstallAnswer : u8 { Installed, Declined };

}  // namespace reboot::compat
