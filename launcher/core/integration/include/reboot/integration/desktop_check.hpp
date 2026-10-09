#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::integration {

// Play's rule: OsSession on Windows and macOS; Display on Linux, whose systemd --user engine has no session id.
enum class DesktopCheck : u8 { OsSession, Display };

}  // namespace rb::integration
