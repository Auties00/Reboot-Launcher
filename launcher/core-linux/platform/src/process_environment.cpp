#include "process_environment.hpp"

#include <algorithm>
#include <cstdlib>
#include <unistd.h>

namespace reboot::os_linux::platform {

std::optional<std::string_view> env_value(const char* name) noexcept {
    const char* const value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    return std::string_view{value};
}

std::vector<std::pair<std::string, std::string>> current_environment() {
    std::vector<std::pair<std::string, std::string>> vars;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view text{*entry};
        const std::size_t equals = text.find('=');
        if (equals == std::string_view::npos || equals == 0) continue;
        vars.emplace_back(std::string(text.substr(0, equals)), std::string(text.substr(equals + 1)));
    }
    return vars;
}

std::vector<std::string> envp_strings(const std::vector<std::pair<std::string, std::string>>& vars) {
    std::vector<std::string> out;
    out.reserve(vars.size());
    for (std::size_t i = 0; i < vars.size(); ++i) {
        const std::string& name = vars[i].first;
        const bool overridden = std::any_of(vars.begin() + static_cast<std::ptrdiff_t>(i) + 1, vars.end(),
                                            [&](const auto& later) { return later.first == name; });
        if (!overridden) out.push_back(name + "=" + vars[i].second);
    }
    return out;
}

}  // namespace reboot::os_linux::platform
