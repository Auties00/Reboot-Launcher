#include "reboot/logging/log_format.hpp"

#include <array>
#include <chrono>
#include <format>
#include <type_traits>
#include <variant>

namespace rb::logging {

namespace {

constexpr std::string_view kContinuationIndent = "    ";

void append_escaped(std::string& out, std::string_view text) {
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\n') {
            out += '\n';
            out += kContinuationIndent;
        } else if ((byte < 0x20 && c != '\t') || byte == 0x7f) {
            out += std::format("\\x{:02x}", byte);
        } else {
            out += c;
        }
    }
}

void append_arg(std::string& out, const Arg& arg) {
    std::visit(
        [&out]<class V>(const V& value) {
            if constexpr (std::is_same_v<V, std::string>) out += std::format("\"{}\"", value);
            else if constexpr (std::is_same_v<V, bool>) out += value ? "true" : "false";
            else if constexpr (std::is_same_v<V, std::chrono::milliseconds>) out += std::format("{}ms", value.count());
            else if constexpr (std::is_same_v<V, WirePath>) out += std::format("\"{}\"", value.display);
            else if constexpr (std::is_same_v<V, SemVer>) out += value.to_string();
            else out += std::format("{}", value);
        },
        arg);
}

void append_diagnostic(std::string& out, const Diagnostic& diag) {
    out += diag.id;
    out += '(';
    bool first = true;
    for (const auto& [name, value] : diag.args) {
        if (!first) out += ", ";
        first = false;
        out += name;
        out += '=';
        append_arg(out, value);
    }
    out += ')';
    if (diag.detail) out += std::format(" detail=\"{}\"", *diag.detail);
    if (diag.os_error) {
        const std::string_view origin = diag.os_error->origin == SystemError::Origin::Host ? "host" : "guest";
        out += std::format(" os={}:{}", origin, diag.os_error->code);
    }
    for (const Diagnostic& cause : diag.causes) {
        out += " <- ";
        append_diagnostic(out, cause);
    }
}

}  // namespace

std::string_view level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string_view category_name(LogCategory category) noexcept {
    static constexpr std::array<std::string_view, 14> kNames{
        "engine",  "ipc",  "net",     "storage", "builds", "play",   "host",
        "backend", "game_output", "wine", "browser", "update", "client", "ui"};
    const auto index = static_cast<std::size_t>(category);
    return index < kNames.size() ? kNames[index] : "?";
}

LogLevel level_for(Severity severity) noexcept {
    switch (severity) {
        case Severity::Info: return LogLevel::Info;
        case Severity::Warning: return LogLevel::Warn;
        case Severity::Error: return LogLevel::Error;
    }
    return LogLevel::Error;
}

std::string format_log_line(const LogRecord& record) {
    std::string line = std::format("{:%Y-%m-%dT%H:%M:%S}Z {:<5} {}",
                                   std::chrono::floor<std::chrono::milliseconds>(record.time),
                                   level_name(record.level), category_name(record.category));
    if (record.session) {
        line += ' ';
        line += format_uuid(record.session->value);
    }
    line += ' ';
    append_escaped(line, record.text);
    line += '\n';
    return line;
}

std::string format_diagnostic(const Diagnostic& diag) {
    std::string out;
    append_diagnostic(out, diag);
    return out;
}

}  // namespace rb::logging
