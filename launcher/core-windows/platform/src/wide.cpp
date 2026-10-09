#include "wide.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "reboot/foundation/text.hpp"

namespace reboot::os_windows::platform {

namespace {

constexpr std::wstring_view kExtendedPrefix = L"\\\\?\\";
constexpr std::wstring_view kUncPrefix = L"\\\\";
// 1601-01-01 to 1970-01-01 in FILETIME ticks.
constexpr u64 kUnixEpochTicks = 116444736000000000ull;
using Ticks = std::chrono::duration<i64, std::ratio<1, 10'000'000>>;

[[nodiscard]] wchar_t upper_ascii(wchar_t c) noexcept {
    return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - (L'a' - L'A')) : c;
}

// Hidden per-drive entries such as "=C:=C:\dir" start with '=', so the name ends at a later one.
[[nodiscard]] std::wstring_view entry_name(std::wstring_view entry) noexcept {
    const std::size_t equals = entry.find(L'=', 1);
    return equals == std::wstring_view::npos ? entry : entry.substr(0, equals);
}

[[nodiscard]] bool names_equal(std::wstring_view a, std::wstring_view b) noexcept {
    return std::ranges::equal(a, b, {}, upper_ascii, upper_ascii);
}

[[nodiscard]] bool name_less(std::wstring_view a, std::wstring_view b) noexcept {
    return std::ranges::lexicographical_compare(a, b, {}, upper_ascii, upper_ascii);
}

[[nodiscard]] NativePath absolute_normal(const NativePath& path) {
    NativePath absolute = path;
    if (!absolute.is_absolute()) {
        std::error_code error;
        absolute = std::filesystem::absolute(path, error);
        if (error) absolute = path;
    }
    return absolute.lexically_normal().make_preferred();
}

}  // namespace

std::wstring widen(std::string_view utf8) {
    const std::u16string units = utf8_to_utf16(utf8);
    return {units.begin(), units.end()};
}

std::string narrow(std::wstring_view wide) {
    std::u16string units(wide.size(), u'\0');
    std::ranges::transform(wide, units.begin(), [](wchar_t c) { return static_cast<char16_t>(c); });
    return utf16_to_utf8(units);
}

std::wstring shell_path(const NativePath& path) {
    std::wstring text = absolute_normal(path).native();
    if (text.starts_with(kExtendedPrefix)) text.erase(0, kExtendedPrefix.size());
    // Keep "C:\" whole; drop any other trailing separator.
    while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
    return text;
}

std::wstring extended_path(const NativePath& path) {
    std::wstring text = absolute_normal(path).native();
    if (text.starts_with(kExtendedPrefix)) return text;
    while (text.size() > 3 && text.back() == L'\\') text.pop_back();
    if (text.starts_with(kUncPrefix)) return std::wstring(kExtendedPrefix) + L"UNC\\" + text.substr(kUncPrefix.size());
    return std::wstring(kExtendedPrefix) + text;
}

void append_argument(std::wstring& line, std::wstring_view arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        line += arg;
        return;
    }
    line += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // Backslashes double only before a quote, which is then escaped itself.
        if (c == L'"') {
            line.append(backslashes * 2 + 1, L'\\');
        } else {
            line.append(backslashes, L'\\');
        }
        backslashes = 0;
        line += c;
    }
    // The closing quote follows, so trailing backslashes double.
    line.append(backslashes * 2, L'\\');
    line += L'"';
}

std::wstring build_command_line(const NativePath& exe, std::span<const std::string> args) {
    std::wstring line;
    append_argument(line, exe.native());
    for (const std::string& arg : args) {
        line += L' ';
        append_argument(line, widen(arg));
    }
    return line;
}

std::wstring build_environment_block(std::span<const std::wstring> base, const EnvVars& overlay) {
    std::vector<std::wstring> entries;
    entries.reserve(base.size() + overlay.size());
    for (const std::wstring& entry : base)
        if (!entry.empty()) entries.push_back(entry);
    for (const auto& [name, value] : overlay) {
        const std::wstring wide_name = widen(name);
        std::erase_if(entries, [&](const std::wstring& entry) { return names_equal(entry_name(entry), wide_name); });
        entries.push_back(wide_name + L"=" + widen(value));
    }
    std::ranges::stable_sort(entries, [](const std::wstring& a, const std::wstring& b) {
        return name_less(entry_name(a), entry_name(b));
    });
    std::wstring block;
    for (const std::wstring& entry : entries) {
        block += entry;
        block += L'\0';
    }
    // An empty block still needs its two terminating NULs.
    if (block.empty()) block += L'\0';
    block += L'\0';
    return block;
}

std::chrono::system_clock::time_point from_filetime(u64 ticks) noexcept {
    const Ticks since_unix{static_cast<i64>(ticks) - static_cast<i64>(kUnixEpochTicks)};
    return std::chrono::system_clock::time_point{std::chrono::duration_cast<std::chrono::system_clock::duration>(since_unix)};
}

u64 to_filetime(std::chrono::system_clock::time_point time) noexcept {
    const Ticks since_unix = std::chrono::duration_cast<Ticks>(time.time_since_epoch());
    return static_cast<u64>(since_unix.count() + static_cast<i64>(kUnixEpochTicks));
}

bool same_creation_time(std::chrono::system_clock::time_point a, std::chrono::system_clock::time_point b) noexcept {
    using std::chrono::floor;
    using std::chrono::microseconds;
    return floor<microseconds>(a) == floor<microseconds>(b);
}

}  // namespace reboot::os_windows::platform
