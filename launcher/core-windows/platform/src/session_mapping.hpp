#pragma once

#include <string_view>

#include "reboot/contracts/winhost.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/os_windows/win32session/win32_session.hpp"
#include "reboot/ports/session_host.hpp"

namespace rb::os_windows::platform {

// UTF-16LE bytes, as the winhost contract carries Windows strings; no terminator.
[[nodiscard]] contracts::winhost::Bytes utf16_bytes(std::wstring_view text);
[[nodiscard]] NativePath path_of(const contracts::winhost::Bytes& utf16le);

// win32session's events in the port's vocabulary; codes are Host errors, since injection ran here.
[[nodiscard]] ports::SessionHostEvent to_port_event(const win32session::SessionEvent& event);

// The Win32 call each step stands for, or the payload messages for a quarantined or altered DLL.
[[nodiscard]] Diagnostic spawn_diagnostic(const win32session::SpawnError& error, const NativePath& exe);
[[nodiscard]] Diagnostic inject_diagnostic(const win32session::InjectError& error, const NativePath& dll);

}  // namespace rb::os_windows::platform
