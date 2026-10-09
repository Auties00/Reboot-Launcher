#include "win_modules.hpp"

// Core code includes no OS header, so the two kernel32 calls are declared here, alone in this file.
#ifdef _WIN32
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleW(const wchar_t* module_name);
extern "C" __declspec(dllimport) void* __stdcall LoadLibraryW(const wchar_t* file_name);
#endif

namespace reboot::testing::win_modules {

bool loaded(const wchar_t* file_name) noexcept {
#ifdef _WIN32
    return GetModuleHandleW(file_name) != nullptr;
#else
    (void)file_name;
    return false;
#endif
}

bool load(const wchar_t* path) noexcept {
#ifdef _WIN32
    return LoadLibraryW(path) != nullptr;
#else
    (void)path;
    return false;
#endif
}

}  // namespace reboot::testing::win_modules
