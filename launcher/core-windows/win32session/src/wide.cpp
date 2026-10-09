#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "wide.hpp"

namespace reboot::os_windows::win32session {

std::wstring to_wide(const Bytes& utf16le) {
    std::wstring out(utf16le.size() / 2, L'\0');
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<wchar_t>(utf16le[2 * i] | (utf16le[2 * i + 1] << 8));
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

void terminate_env_block(Bytes& block) {
    if (block.size() % 2 != 0) block.push_back(0);
    const auto nul_unit = [&](std::size_t from_end) {
        const std::size_t at = block.size() - 2 * from_end;
        return block.size() >= 2 * from_end && block[at] == 0 && block[at + 1] == 0;
    };
    while (!(nul_unit(1) && nul_unit(2))) block.insert(block.end(), {0, 0});
}

void wipe(std::wstring& text) noexcept {
    if (!text.empty()) SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
}

void wipe(Bytes& bytes) noexcept {
    if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size());
}

namespace {

// One argument, quoted per the CommandLineToArgvW rules: backslashes before a quote double, the
// closing quote escapes, and empty or space-bearing args get quotes.
void append_arg(std::wstring& line, const std::wstring& arg) {
    const bool needs_quotes = arg.empty() || arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
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
    // Worst case per argument: every unit escaped, two quotes and a separator. Reserving it up
    // front means the line never reallocates and leaves a stale copy in freed memory.
    std::size_t bound = exe_utf16.size() + 3;
    for (const auto& arg : argv_utf16) bound += arg.size() + 3;
    std::wstring line;
    line.reserve(bound);
    std::wstring exe = to_wide(exe_utf16);
    append_arg(line, exe);
    for (const auto& arg_utf16 : argv_utf16) {
        std::wstring arg = to_wide(arg_utf16);
        line += L' ';
        append_arg(line, arg);
        wipe(arg);
    }
    return line;
}

}  // namespace reboot::os_windows::win32session
