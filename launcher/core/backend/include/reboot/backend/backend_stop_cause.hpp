#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Why a stop was asked for; none of them is ever reported as a crash.
enum class BackendStopCause : u8 { UserStop, LastLease, Reconfigure, Reset, Shutdown };

}  // namespace rb::backend
