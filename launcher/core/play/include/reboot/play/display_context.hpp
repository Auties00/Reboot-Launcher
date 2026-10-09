#pragma once

#include <string_view>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::play {

// What proves a caller shares the engine's desktop. OsSession: the Windows session id or the Aqua
// session. Display: Linux, where a usable DISPLAY or WAYLAND_DISPLAY is what counts.
enum class SessionMatch : u8 { OsSession, Display };

// Covers game-launch.orchestration.
// The caller's desktop as its Hello or the call sent it; display_env feeds EnvBuilder's client layer.
using DisplayContext = contracts::ipc::CallerContext;

// play.wrong_session when OsSession differs from `engine_os_session` (ISystemInfo::os_session);
// play.no_display when Display finds neither DISPLAY nor WAYLAND_DISPLAY in display_env.
[[nodiscard]] Result<void> check_display(const DisplayContext& caller, std::string_view engine_os_session,
                                         SessionMatch match);

}  // namespace rb::play
