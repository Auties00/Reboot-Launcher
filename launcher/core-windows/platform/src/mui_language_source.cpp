#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/mui_language_source.hpp"

#include "wide.hpp"

namespace rb::os_windows::platform {

std::vector<std::string> MuiLanguageSource::preferred_ui_languages() {
    ULONG count = 0;
    ULONG size = 0;
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) == FALSE || size == 0) return {};
    std::wstring list(size, L' ');
    if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, list.data(), &size) == FALSE) return {};
    list.resize(size);
    return split_multi_string(list);
}

}  // namespace rb::os_windows::platform
