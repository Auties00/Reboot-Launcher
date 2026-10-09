#include "update_entries.hpp"

#include <cstddef>
#include <vector>

namespace rb::os_linux::platform {

namespace {

[[nodiscard]] std::vector<std::string_view> components(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        if (!part.empty() && part != ".") parts.push_back(part);
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return parts;
}

}  // namespace

std::optional<std::string> safe_entry_path(std::string_view path) {
    if (path.starts_with('/')) return std::nullopt;
    std::string safe;
    for (const std::string_view part : components(path)) {
        if (part == "..") return std::nullopt;
        if (!safe.empty()) safe += '/';
        safe += part;
    }
    return safe;
}

std::string_view top_component(std::string_view safe_path) noexcept {
    return safe_path.substr(0, safe_path.find('/'));
}

bool link_stays_inside(std::string_view entry, std::string_view target) {
    if (target.empty() || target.starts_with('/')) return false;
    std::vector<std::string_view> resolved = components(entry);
    if (!resolved.empty()) resolved.pop_back();
    for (const std::string_view part : components(target)) {
        if (part != "..") {
            resolved.push_back(part);
        } else if (resolved.empty()) {
            return false;
        } else {
            resolved.pop_back();
        }
    }
    return true;
}

}  // namespace rb::os_linux::platform
