#include "reboot/process/windows_command_line.hpp"

#include <cstddef>
#include <string_view>

namespace reboot::process {

namespace {

[[nodiscard]] bool needs_quotes(std::string_view text) {
    return text.empty() || text.find_first_of(" \t\n\v\"") != std::string_view::npos;
}

// The CRT rule: backslashes are literal unless they precede a quote, so those and the ones before
// the closing quote are doubled.
void append_quoted(std::string& out, std::string_view text) {
    out.push_back('"');
    std::size_t backslashes = 0;
    for (const char c : text) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
        } else {
            out.append(backslashes, '\\');
        }
        backslashes = 0;
        out.push_back(c);
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
}

void append_argument(std::string& out, std::string_view arg) {
    if (!needs_quotes(arg)) {
        out.append(arg);
        return;
    }
    const std::size_t equals = arg.find('=');
    if (arg.starts_with('-') && equals != std::string_view::npos && !needs_quotes(arg.substr(0, equals))) {
        out.append(arg.substr(0, equals + 1));
        append_quoted(out, arg.substr(equals + 1));
        return;
    }
    append_quoted(out, arg);
}

}  // namespace

std::string quote_windows_args(std::span<const std::string> argv) {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) out.push_back(' ');
        const std::string_view arg = argv[i];
        // CommandLineToArgvW reads the program name without escapes; it cannot hold a quote.
        if (i == 0) {
            const bool quote = arg.empty() || arg.find_first_of(" \t") != std::string_view::npos;
            if (quote) out.push_back('"');
            out.append(arg);
            if (quote) out.push_back('"');
            continue;
        }
        append_argument(out, arg);
    }
    return out;
}

}  // namespace reboot::process
