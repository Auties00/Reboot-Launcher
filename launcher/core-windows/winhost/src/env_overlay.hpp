#pragma once

#include "reboot/contracts/winhost.hpp"

namespace reboot::os_windows::winhost {

// Lays `overlay` (a UTF-16LE environment block) over `base` (a double-NUL-terminated block): an
// overlay variable replaces the base one of the same name, compared case-insensitively. The
// result is sorted by name, as CreateProcessW expects, and double-NUL terminated. The overlay
// may hold a token, so the one intermediate copy is wiped.
[[nodiscard]] contracts::winhost::Bytes overlay_environment(const wchar_t* base,
                                                           const contracts::winhost::Bytes& overlay);

// overlay_environment over this process's own environment.
[[nodiscard]] contracts::winhost::Bytes overlay_own_environment(const contracts::winhost::Bytes& overlay);

}  // namespace reboot::os_windows::winhost
