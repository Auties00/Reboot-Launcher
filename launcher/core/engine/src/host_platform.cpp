#include "host_platform.hpp"

#include <cstdlib>
#include <string_view>

#include "reboot/foundation/native_path.hpp"

namespace rb::engine {

std::optional<std::string> own_environment(const char* name) {
#ifdef _WIN32
    // The wide environment, so a value outside the ANSI code page survives.
    const std::string_view narrow(name);
    const std::wstring key(narrow.begin(), narrow.end());
    wchar_t* value = nullptr;
    std::size_t size = 0;
    if (_wdupenv_s(&value, &size, key.c_str()) != 0 || value == nullptr) return std::nullopt;
    const std::wstring wide(value);
    std::free(value);
    if (wide.empty()) return std::nullopt;
    return display_utf8(NativePath(wide));
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
#endif
}

}  // namespace rb::engine
