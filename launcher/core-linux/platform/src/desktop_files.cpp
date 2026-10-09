#include "desktop_files.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace rb::os_linux::platform {

namespace {

constexpr std::string_view kDesktopGroup = "[Desktop Entry]";
constexpr std::string_view kDefaultsGroup = "[Default Applications]";
// Characters the Desktop Entry spec reserves inside an Exec argument.
constexpr std::string_view kReserved = " \t\n\"'\\><~|&;$*?#()`";

[[nodiscard]] std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        lines.push_back(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    }
    return lines;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const std::size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    const std::size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

// key=value with spaces around '=' ignored; nullopt for other lines.
[[nodiscard]] std::optional<std::pair<std::string_view, std::string_view>> key_value(std::string_view line) {
    const std::size_t equals = line.find('=');
    if (equals == std::string_view::npos) return std::nullopt;
    return std::pair{trim(line.substr(0, equals)), trim(line.substr(equals + 1))};
}

[[nodiscard]] bool is_group(std::string_view line) noexcept { return line.starts_with('['); }

// The file format's string escapes: \s, \n, \t, \r and \\; any other backslash stays.
[[nodiscard]] std::string unescape_value(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            const char replaced = next == 's' ? ' ' : next == 'n' ? '\n' : next == 't' ? '\t' : next == 'r' ? '\r' : next == '\\' ? '\\' : '\0';
            if (replaced != '\0') {
                out += replaced;
                ++i;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

[[nodiscard]] std::string escape_value(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}

[[nodiscard]] std::string double_percent(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += c;
        if (c == '%') out += '%';
    }
    return out;
}

}  // namespace

DesktopEntryKeys parse_desktop_entry(std::string_view text) {
    DesktopEntryKeys keys;
    bool in_group = false;
    for (const std::string_view raw : lines_of(text)) {
        const std::string_view line = trim(raw);
        if (is_group(line)) {
            in_group = line == kDesktopGroup;
            continue;
        }
        if (!in_group || line.starts_with('#')) continue;
        const auto entry = key_value(line);
        if (!entry) continue;
        if (entry->first == "Exec")
            keys.exec = unescape_value(entry->second);
        else if (entry->first == "Hidden")
            keys.hidden = std::string(entry->second);
        else if (entry->first == "X-GNOME-Autostart-enabled")
            keys.autostart_enabled = std::string(entry->second);
    }
    return keys;
}

bool opted_out(const DesktopEntryKeys& keys) noexcept {
    return keys.hidden == "true" || keys.autostart_enabled == "false";
}

std::string desktop_exec(const std::vector<std::string>& args, const std::vector<std::string>& field_codes) {
    std::string exec;
    for (const std::string& arg : args) {
        if (!exec.empty()) exec += ' ';
        if (std::ranges::contains(field_codes, arg)) {
            exec += arg;
            continue;
        }
        const std::string doubled = double_percent(arg);
        if (!doubled.empty() && doubled.find_first_of(kReserved) == std::string::npos) {
            exec += doubled;
            continue;
        }
        exec += '"';
        for (const char c : doubled) {
            if (c == '"' || c == '`' || c == '$' || c == '\\') exec += '\\';
            exec += c;
        }
        exec += '"';
    }
    return escape_value(exec);
}

std::optional<std::vector<std::string>> split_desktop_exec(std::string_view exec) {
    std::vector<std::string> args;
    std::string current;
    bool in_token = false;
    bool quoted = false;
    for (std::size_t i = 0; i < exec.size(); ++i) {
        const char c = exec[i];
        if (quoted) {
            if (c == '\\' && i + 1 < exec.size()) {
                current += exec[++i];
            } else if (c == '"') {
                quoted = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            quoted = true;
            in_token = true;
        } else if (c == ' ' || c == '\t' || c == '\n') {
            if (in_token) args.push_back(std::exchange(current, {}));
            in_token = false;
        } else {
            current += c;
            in_token = true;
        }
    }
    if (quoted) return std::nullopt;
    if (in_token) args.push_back(std::move(current));
    for (std::string& arg : args) {
        std::string single;
        for (std::size_t i = 0; i < arg.size(); ++i) {
            single += arg[i];
            if (arg[i] == '%' && i + 1 < arg.size() && arg[i + 1] == '%') ++i;
        }
        arg = std::move(single);
    }
    return args;
}

std::string render_desktop_entry(std::string_view name, const std::vector<std::string>& exec_args,
                                 const std::vector<std::string>& extra_lines, const DesktopEntryKeys& keep) {
    std::string text = std::string(kDesktopGroup) + "\nType=Application\nName=" + escape_value(name) +
                       "\nExec=" + desktop_exec(exec_args, {"%u"}) + "\nTerminal=false\n";
    for (const std::string& line : extra_lines) text += line + "\n";
    if (keep.hidden) text += "Hidden=" + *keep.hidden + "\n";
    if (keep.autostart_enabled) text += "X-GNOME-Autostart-enabled=" + *keep.autostart_enabled + "\n";
    return text;
}

std::string join_command(const std::vector<std::string>& args) {
    std::string command;
    for (const std::string& arg : args) {
        if (!command.empty()) command += ' ';
        if (!arg.empty() && arg.find_first_of(" \"") == std::string::npos) {
            command += arg;
            continue;
        }
        command += '"';
        for (const char c : arg) {
            if (c == '"') command += '\\';
            command += c;
        }
        command += '"';
    }
    return command;
}

std::optional<NativePath> program_of(const std::vector<std::string>& args) {
    std::size_t at = 0;
    if (!args.empty() && NativePath{args.front()}.filename() == "env") {
        at = 1;
        while (at < args.size() && !args[at].starts_with('/') && args[at].find('=') != std::string::npos) ++at;
    }
    if (at >= args.size()) return std::nullopt;
    return NativePath{args[at]};
}

std::optional<std::string> mimeapps_default(std::string_view text, std::string_view mime) {
    bool in_group = false;
    for (const std::string_view raw : lines_of(text)) {
        const std::string_view line = trim(raw);
        if (is_group(line)) {
            in_group = line == kDefaultsGroup;
            continue;
        }
        if (!in_group) continue;
        const auto entry = key_value(line);
        if (!entry || entry->first != mime) continue;
        std::string_view ids = entry->second;
        while (!ids.empty()) {
            const std::size_t end = ids.find(';');
            const std::string_view id = trim(ids.substr(0, end));
            if (!id.empty()) return std::string(id);
            if (end == std::string_view::npos) break;
            ids.remove_prefix(end + 1);
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string mimeapps_with_default(std::string_view text, std::string_view mime,
                                  const std::optional<std::string>& desktop_id) {
    const std::string wanted = desktop_id ? std::string(mime) + "=" + *desktop_id + ";" : std::string();
    std::string out;
    bool in_group = false;
    bool seen_group = false;
    bool written = !desktop_id;
    const auto flush_group = [&] {
        if (in_group && !written) {
            out += wanted + "\n";
            written = true;
        }
    };
    for (const std::string_view raw : lines_of(text)) {
        const std::string_view line = trim(raw);
        if (is_group(line)) {
            flush_group();
            in_group = line == kDefaultsGroup;
            seen_group = seen_group || in_group;
        } else if (in_group) {
            const auto entry = key_value(line);
            if (entry && entry->first == mime) {
                if (!written) {
                    out += wanted + "\n";
                    written = true;
                }
                continue;
            }
        }
        out += std::string(raw) + "\n";
    }
    flush_group();
    if (!seen_group && desktop_id) {
        if (!out.empty() && !out.ends_with("\n\n")) out += "\n";
        out += std::string(kDefaultsGroup) + "\n" + wanted + "\n";
    }
    return out;
}

}  // namespace rb::os_linux::platform
