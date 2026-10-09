#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_system_info.hpp"

#include "os_version.hpp"
#include "process_token.hpp"

namespace reboot::os_windows::platform {

WindowsSystemInfo::WindowsSystemInfo() : elevated_(process_elevated()) {
    const OsVersion version = read_os_version();
    os_.name = "windows";
    os_.version = std::to_string(version.major) + "." + std::to_string(version.minor);
    os_.build = std::to_string(version.build);
    os_.arch = native_arch();
    DWORD session = 0;
    os_session_ = ProcessIdToSessionId(GetCurrentProcessId(), &session) != 0 ? std::to_string(session) : "0";
}

ports::OsInfo WindowsSystemInfo::os() const { return os_; }

bool WindowsSystemInfo::elevated() const { return elevated_; }

std::string WindowsSystemInfo::os_session() const { return os_session_; }

}  // namespace reboot::os_windows::platform
