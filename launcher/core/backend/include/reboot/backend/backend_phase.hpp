#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Running for Local and Remote means the last probe answered.
enum class BackendPhase : u8 { Stopped, Starting, Running, Stopping, Restarting, Failed };

}  // namespace rb::backend
