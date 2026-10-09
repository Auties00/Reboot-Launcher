#include "systemd_units.hpp"

#include <cstddef>
#include <utility>

namespace reboot::os_linux::platform {

namespace {

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const std::size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    const std::size_t end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

[[nodiscard]] std::string quote_value(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// Specifiers expand in every value, so a literal '%' is "%%".
[[nodiscard]] std::string escape_specifiers(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out += c;
        if (c == '%') out += '%';
    }
    return out;
}

[[nodiscard]] std::string environment_line(std::string_view name, const NativePath& value) {
    return "Environment=" + quote_value(escape_specifiers(std::string(name) + "=" + value.string())) + "\n";
}

}  // namespace

std::string engine_listen_stream(std::string_view hash16) {
    return "%t/reboot-launcher/" + std::string(hash16) + ".sock";
}

std::string render_engine_socket(std::string_view hash16) {
    return "[Unit]\nDescription=Reboot Launcher engine socket\n\n[Socket]\nListenStream=" + engine_listen_stream(hash16) +
           "\nSocketMode=0600\nDirectoryMode=0700\n\n[Install]\nWantedBy=sockets.target\n";
}

std::string render_engine_service(const std::vector<std::string>& exec_start, const NativePath& data_home,
                                  const NativePath& cache_home, const NativePath& state_home) {
    std::string exec;
    for (const std::string& arg : exec_start) {
        if (!exec.empty()) exec += ' ';
        exec += unit_quote(arg);
    }
    // A burst of three, so an update that crashes before its self-test stops being restarted.
    // Type=exec fails the start when the stable entry cannot run; systemd before 240 ignores it.
    return "[Unit]\nDescription=Reboot Launcher engine\nRequires=" + std::string(kEngineSocketUnit) +
           "\nAfter=" + std::string(kEngineSocketUnit) + "\nStartLimitBurst=3\n\n[Service]\nType=exec\nExecStart=" +
           exec + "\n" + environment_line("XDG_DATA_HOME", data_home) + environment_line("XDG_CACHE_HOME", cache_home) +
           environment_line("XDG_STATE_HOME", state_home) + "Restart=on-failure\n";
}

std::optional<std::string> unit_value(std::string_view text, std::string_view key) {
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || trim(line.substr(0, equals)) != key) continue;
        return std::string(trim(line.substr(equals + 1)));
    }
    return std::nullopt;
}

std::string unit_quote(std::string_view arg) {
    std::string escaped;
    for (const char c : arg) {
        escaped += c;
        if (c == '%' || c == '$') escaped += c;
    }
    if (!escaped.empty() && escaped.find_first_of(" \t\"'\\;") == std::string::npos) return escaped;
    return quote_value(escaped);
}

std::optional<std::vector<std::string>> split_unit_command(std::string_view value) {
    std::vector<std::string> args;
    std::string current;
    bool in_token = false;
    char quote = '\0';
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (quote != '\0') {
            if (c == '\\' && i + 1 < value.size()) {
                const char next = value[++i];
                current += next == 'n' ? '\n' : next == 't' ? '\t' : next;
            } else if (c == quote) {
                quote = '\0';
            } else {
                current += c;
            }
        } else if (c == '"' || c == '\'') {
            quote = c;
            in_token = true;
        } else if (c == ' ' || c == '\t') {
            if (in_token) args.push_back(std::exchange(current, {}));
            in_token = false;
        } else if (c == '\\' && i + 1 < value.size()) {
            current += value[++i];
            in_token = true;
        } else {
            current += c;
            in_token = true;
        }
    }
    if (quote != '\0') return std::nullopt;
    if (in_token) args.push_back(std::move(current));
    for (std::string& arg : args) {
        std::string single;
        for (std::size_t i = 0; i < arg.size(); ++i) {
            single += arg[i];
            if ((arg[i] == '%' || arg[i] == '$') && i + 1 < arg.size() && arg[i + 1] == arg[i]) ++i;
        }
        arg = std::move(single);
    }
    return args;
}

}  // namespace reboot::os_linux::platform
