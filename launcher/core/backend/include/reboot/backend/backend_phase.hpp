#pragma once

#include "reboot/foundation/types.hpp"

namespace reboot::backend {

// Running for Local and Remote means the last probe answered.
enum class BackendPhase : u8 { Stopped, Starting, Running, Stopping, Restarting, Failed };

}  // namespace reboot::backend
