#include "os_release.hpp"

#include <cstddef>
#include <utility>

namespace rb::os_linux::platform {

namespace {

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const std::size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    const std::size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

[[nodiscard]] std::string unquote(std::string_view value) {
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        const char quote = value.front();
        value = value.substr(1, value.size() - 2);
        std::string out;
        out.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            // Single quotes take everything literally, as in a shell.
            if (quote == '"' && value[i] == '\\' && i + 1 < value.size()) ++i;
            out += value[i];
        }
        return out;
    }
    return std::string(value);
}

}  // namespace

OsRelease parse_os_release(std::string_view text) {
    OsRelease release;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        if (line.empty() || line.front() == '#') continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = line.substr(0, equals);
        std::string value = unquote(trim(line.substr(equals + 1)));
        if (key == "NAME")
            release.name = std::move(value);
        else if (key == "VERSION_ID")
            release.version_id = std::move(value);
        else if (key == "BUILD_ID")
            release.build_id = std::move(value);
    }
    return release;
}

bool environ_block_has(std::string_view block, std::string_view name) noexcept {
    while (!block.empty()) {
        const std::size_t end = block.find('\0');
        const std::string_view entry = block.substr(0, end);
        if (entry.size() > name.size() && entry.starts_with(name) && entry[name.size()] == '=') return true;
        if (end == std::string_view::npos) break;
        block.remove_prefix(end + 1);
    }
    return false;
}

}  // namespace rb::os_linux::platform
