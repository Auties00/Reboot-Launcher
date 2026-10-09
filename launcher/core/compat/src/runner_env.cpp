#include "reboot/compat/runner_env.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "dxmt_builtin_dlls.hpp"
#include "reboot/foundation/text.hpp"

namespace reboot::compat {

namespace {

constexpr std::string_view kOverridesName = "WINEDLLOVERRIDES";
constexpr std::string_view kMenuBuilder = "winemenubuilder";

// Wine accepts "*name" and "name.dll" for the same module.
[[nodiscard]] std::string_view module_of(std::string_view name) noexcept {
    if (name.starts_with('*')) name.remove_prefix(1);
    for (const std::string_view suffix : {std::string_view(".dll"), std::string_view(".exe")})
        if (name.size() > suffix.size() && iequals_ascii(name.substr(name.size() - suffix.size()), suffix))
            name.remove_suffix(suffix.size());
    return name;
}

[[nodiscard]] bool dropped(RunnerKind kind, std::string_view name) {
    const std::string_view module = module_of(name);
    if (iequals_ascii(module, kMenuBuilder)) return true;
    if (kind != RunnerKind::MacRuntime) return false;
    return std::ranges::any_of(kDxmtBuiltinDlls, [&](std::string_view dll) { return iequals_ascii(module, dll); });
}

// "a,b=n,b;c=d": each entry is a module list, then an optional mode after '='.
[[nodiscard]] std::string merged_overrides(RunnerKind kind, std::string_view inherited) {
    std::string out;
    while (!inherited.empty()) {
        const std::size_t end = std::min(inherited.find(';'), inherited.size());
        const std::string_view entry = inherited.substr(0, end);
        inherited.remove_prefix(std::min(end + 1, inherited.size()));

        const std::size_t equals = std::min(entry.find('='), entry.size());
        std::string_view modules = entry.substr(0, equals);
        std::string kept;
        while (!modules.empty()) {
            const std::size_t comma = std::min(modules.find(','), modules.size());
            const std::string_view name = modules.substr(0, comma);
            modules.remove_prefix(std::min(comma + 1, modules.size()));
            if (name.empty() || dropped(kind, name)) continue;
            if (!kept.empty()) kept += ',';
            kept += name;
        }
        if (kept.empty()) continue;
        out += kept;
        out += entry.substr(equals);
        out += ';';
    }
    out += kWineDllOverrides;
    return out;
}

}  // namespace

ports::EnvBlock runner_layer(RunnerKind kind, const ports::RuntimeLayout& layout) {
    ports::EnvBlock layer;
    std::string inherited;
    for (const auto& [name, value] : layout.env) {
        if (name == kOverridesName) {
            if (!inherited.empty() && !value.empty()) inherited += ';';
            inherited += value;
            continue;
        }
        layer.vars.emplace_back(name, value);
    }
    layer.vars.emplace_back(kOverridesName, merged_overrides(kind, inherited));
    return layer;
}

}  // namespace reboot::compat
