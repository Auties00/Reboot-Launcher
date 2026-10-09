#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "os_version.hpp"

namespace rb::os_windows::platform {

namespace {

using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);

}  // namespace

OsVersion read_os_version() noexcept {
    OsVersion version;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return version;
    // Through a generic function pointer, so the cast stays function-to-function.
    const auto generic = reinterpret_cast<void (*)()>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (generic == nullptr) return version;
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof info;
    if (reinterpret_cast<RtlGetVersionFn>(generic)(&info) != 0) return version;
    version.major = info.dwMajorVersion;
    version.minor = info.dwMinorVersion;
    version.build = info.dwBuildNumber;
    return version;
}

std::string native_arch() {
    USHORT process_machine = 0;
    USHORT native_machine = 0;
    if (IsWow64Process2(GetCurrentProcess(), &process_machine, &native_machine) == 0) return "x86_64";
    switch (native_machine) {
        case IMAGE_FILE_MACHINE_ARM64: return "arm64";
        case IMAGE_FILE_MACHINE_I386: return "x86";
        default: return "x86_64";
    }
}

}  // namespace rb::os_windows::platform
