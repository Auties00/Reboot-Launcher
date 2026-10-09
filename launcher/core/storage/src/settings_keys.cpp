#include "reboot/storage/settings_keys.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/text.hpp"

namespace rb::storage {

namespace {

[[nodiscard]] constexpr bool is_alpha(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
[[nodiscard]] constexpr bool is_alnum(char c) noexcept { return is_alpha(c) || (c >= '0' && c <= '9'); }

// RFC 5646 shape only: a 2-3 or 5-8 letter language, then 1-8 alphanumeric subtags.
[[nodiscard]] bool is_well_formed_language_tag(std::string_view tag) noexcept {
    std::size_t start = 0;
    bool first = true;
    while (start <= tag.size()) {
        const std::size_t dash = std::min(tag.find('-', start), tag.size());
        const std::string_view subtag = tag.substr(start, dash - start);
        if (subtag.empty() || subtag.size() > 8) return false;
        if (first) {
            if (subtag.size() < 2 || subtag.size() == 4 || !std::ranges::all_of(subtag, is_alpha)) return false;
            first = false;
        } else if (!std::ranges::all_of(subtag, is_alnum)) {
            return false;
        }
        start = dash + 1;
    }
    return true;
}

// Any control character but tab.
[[nodiscard]] bool has_control(std::string_view text) noexcept {
    return std::ranges::any_of(text, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return (byte < 0x20 && c != '\t') || byte == 0x7F;
    });
}

// CommandLineToArgvW's rule: a quote preceded by an odd number of backslashes is literal.
[[nodiscard]] bool quotes_balanced(std::string_view args) noexcept {
    bool quoted = false;
    std::size_t backslashes = 0;
    for (const char c : args) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"' && backslashes % 2 == 0) quoted = !quoted;
        backslashes = 0;
    }
    return !quoted;
}

}  // namespace

Result<std::string> validate_language_tag(std::string tag) {
    if (tag == kSystemLanguage || is_well_formed_language_tag(tag)) return tag;
    return invalid_input(msg::kInvalidLanguageTag).arg("tag", tag).fail();
}

Result<std::string> validate_launch_args(std::string args) {
    if (!is_valid_utf8(args) || has_control(args) || !quotes_balanced(args))
        return invalid_input(msg::kInvalidLaunchArgs).fail();
    return args;
}

Result<std::optional<NativePath>> validate_auth_dll_path(std::optional<NativePath> path) {
    if (!path) return path;
    if (!path->is_absolute() || !iequals_ascii(display_utf8(path->extension()), ".dll"))
        return invalid_input(msg::kInvalidAuthDllPath).arg("path", *path).fail();
    return path;
}

Result<ports::EnvBlock> parse_env_lines(std::string_view text) {
    ports::EnvBlock block;
    std::size_t line = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        std::string_view entry = text.substr(start, end - start);
        start = end + 1;
        ++line;
        if (entry.ends_with('\r')) entry.remove_suffix(1);
        if (entry.empty()) continue;

        const std::size_t equals = entry.find('=');
        const std::string_view name = entry.substr(0, equals);
        const bool name_ok = !name.empty() && (is_alpha(name.front()) || name.front() == '_') &&
                             std::ranges::all_of(name, [](char c) { return is_alnum(c) || c == '_'; });
        if (equals == std::string_view::npos || !name_ok || !is_valid_utf8(entry))
            return invalid_input(msg::kInvalidEnvLine).arg("line", line).fail();
        const std::string_view value = entry.substr(equals + 1);
        if (has_control(value)) return invalid_input(msg::kInvalidEnvLine).arg("line", line).fail();
        block.vars.emplace_back(std::string(name), std::string(value));
    }
    return block;
}

Result<std::string> validate_env_lines(std::string text) {
    Result<ports::EnvBlock> parsed = parse_env_lines(text);
    if (!parsed) return std::unexpected(std::move(parsed.error()));
    return text;
}

Result<ConsoleKey> validate_console_key(ConsoleKey key) { return ConsoleKey::parse(key.name); }

}  // namespace rb::storage
