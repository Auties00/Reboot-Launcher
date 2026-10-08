#include "wide.hpp"

#include <cstring>

namespace reboot::os_windows::win32session {

std::wstring to_wide(const Bytes& utf16le) {
    std::wstring out(utf16le.size() / 2, L'\0');
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<wchar_t>(utf16le[2 * i] | (utf16le[2 * i + 1] << 8));
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

namespace {

// One argument, quoted per the CommandLineToArgvW rules: backslashes before a quote double, the
// closing quote escapes, and empty or space-bearing args get quotes.
void append_arg(std::wstring& line, const std::wstring& arg) {
    const bool needs_quotes = arg.empty() || arg.find_first_of(L" \t\"") != std::wstring::npos;
    if (!needs_quotes) {
        line += arg;
        return;
    }
    line += L'"';
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == arg.size()) {
            line.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            line.append(backslashes * 2 + 1, L'\\');
            line += L'"';
        } else {
            line.append(backslashes, L'\\');
            line += arg[i];
        }
    }
    line += L'"';
}

}  // namespace

std::wstring build_command_line(const Bytes& exe_utf16, const std::vector<Bytes>& argv_utf16) {
    std::wstring line;
    append_arg(line, to_wide(exe_utf16));
    for (const auto& arg : argv_utf16) {
        line += L' ';
        append_arg(line, to_wide(arg));
    }
    return line;
}

}  // namespace reboot::os_windows::win32session
