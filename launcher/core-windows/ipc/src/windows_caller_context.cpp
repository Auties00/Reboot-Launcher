#include "reboot/os_windows/ipc/windows_caller_context.hpp"

#include <string>

#include "unique_handle.hpp"
#include "win32.hpp"
#include "win32_errors.hpp"

namespace rb::os_windows::ipc {

Result<WindowsCallerContext> WindowsCallerContext::detect() {
    ports::CallerContext context;

    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return std::unexpected(call_failed("ProcessIdToSessionId"));
    context.os_session = std::to_string(session);

    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return std::unexpected(call_failed("OpenProcessToken"));
    const UniqueHandle token{raw};
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size))
        return std::unexpected(call_failed("GetTokenInformation"));
    context.elevated = elevation.TokenIsElevated != 0;

    const HWINSTA station = GetProcessWindowStation();
    if (station == nullptr) return std::unexpected(call_failed("GetProcessWindowStation"));
    USEROBJECTFLAGS flags{};
    if (!GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), nullptr))
        return std::unexpected(call_failed("GetUserObjectInformationW"));
    context.interactive = session != 0 && (flags.dwFlags & WSF_VISIBLE) != 0;

    return WindowsCallerContext{std::move(context)};
}

void WindowsCallerContext::allow_foreground(u32 pid) { AllowSetForegroundWindow(pid); }

}  // namespace rb::os_windows::ipc
