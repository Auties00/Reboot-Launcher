#include "host_platform.hpp"

#include <cstdlib>
#include <string_view>
#include <utility>

#include "reboot/foundation/native_path.hpp"

namespace rb::engine {

integration::IntegrationTargets integration_targets(NativePath engine_exe) {
    integration::IntegrationTargets targets;
    switch (kHostOs) {
        case components::ManifestOs::Windows: targets.flavor = integration::EntryFlavor::Windows; break;
        case components::ManifestOs::MacOs: targets.flavor = integration::EntryFlavor::Apple; break;
        case components::ManifestOs::Linux: targets.flavor = integration::EntryFlavor::FreeDesktop; break;
    }
    targets.engine_exe = std::move(engine_exe);
    return targets;
}

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
