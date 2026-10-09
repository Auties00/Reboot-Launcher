#include "engine_environment.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace rb::os_linux::ipc {
namespace {

constexpr std::string_view kPathName = "PATH";
constexpr std::array<std::string_view, 5> kCopiedNames{
    kPathName, "XDG_DATA_DIRS", "XDG_CONFIG_DIRS", "DBUS_SESSION_BUS_ADDRESS", "LANG",
};
constexpr std::string_view kLocalePrefix = "LC_";
// Relative values would resolve against the engine's cwd, its install directory.
constexpr std::array<std::string_view, 3> kAbsoluteOnlyNames{"TMPDIR", "XDG_CONFIG_HOME", "XDG_RUNTIME_DIR"};

[[nodiscard]] std::string entry(std::string_view name, std::string_view value) {
    std::string text;
    text.reserve(name.size() + 1 + value.size());
    text += name;
    text += '=';
    text += value;
    return text;
}

[[nodiscard]] bool is_copied(std::string_view name, std::string_view value) {
    if (std::ranges::find(kAbsoluteOnlyNames, name) != kAbsoluteOnlyNames.end()) return value.starts_with('/');
    return name.starts_with(kLocalePrefix) || std::ranges::find(kCopiedNames, name) != kCopiedNames.end();
}

}  // namespace

std::vector<std::string> pinned_engine_variables(const EngineEnvironmentInputs& inputs, const DataRoot& root) {
    std::vector<std::string> variables{
        entry("XDG_DATA_HOME", inputs.data_home.string()),
        entry("XDG_CACHE_HOME", inputs.cache_home.string()),
        entry("XDG_STATE_HOME", inputs.state_home.string()),
    };
    if (root.overridden) variables.push_back(entry("REBOOT_LAUNCHER_HOME", root.root.string()));
    return variables;
}

std::vector<std::string> engine_environment(std::span<const std::string_view> inherited,
                                            const EngineEnvironmentInputs& inputs, const DataRoot& root) {
    std::vector<std::string> environment{
        entry("HOME", inputs.home.string()),
        entry("USER", inputs.user_name),
        entry("LOGNAME", inputs.user_name),
        entry("SHELL", inputs.login_shell),
    };
    for (std::string& variable : pinned_engine_variables(inputs, root)) environment.push_back(std::move(variable));

    std::vector<std::string_view> seen;
    bool has_path = false;
    for (const std::string_view variable : inherited) {
        const std::size_t equals = variable.find('=');
        if (equals == std::string_view::npos || equals == 0) continue;
        const std::string_view name = variable.substr(0, equals);
        const std::string_view value = variable.substr(equals + 1);
        // A later duplicate stays hidden even when the first is dropped, as getenv sees it.
        if (std::ranges::find(seen, name) != seen.end()) continue;
        seen.push_back(name);
        if (value.empty() || !is_copied(name, value)) continue;
        environment.emplace_back(variable);
        has_path = has_path || name == kPathName;
    }
    if (!has_path) environment.push_back(entry(kPathName, kDefaultEnginePath));
    return environment;
}

}  // namespace rb::os_linux::ipc
