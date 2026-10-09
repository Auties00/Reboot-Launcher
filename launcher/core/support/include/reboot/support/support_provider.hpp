#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::support {

// Ours: our client DLL and backend. Custom: the user's auth DLL replaces ours.
enum class SupportProvider : u8 { Ours, Custom };

}  // namespace rb::support
