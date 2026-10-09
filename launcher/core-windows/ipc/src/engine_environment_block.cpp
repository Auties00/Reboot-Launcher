#include "engine_environment_block.hpp"

#include <algorithm>
#include <string_view>

namespace reboot::os_windows::ipc {
namespace {

constexpr std::wstring_view kRebootPrefix = L"REBOOT_";
constexpr std::wstring_view kHomeName = L"REBOOT_LAUNCHER_HOME";

[[nodiscard]] wchar_t ascii_upper(wchar_t c) noexcept {
    return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - (L'a' - L'A')) : c;
}

// Hidden per-drive entries such as "=C:=C:\dir" start with '=', so the name ends at a later one.
[[nodiscard]] std::wstring_view name_of(std::wstring_view entry) noexcept { return entry.substr(0, entry.find(L'=', 1)); }

[[nodiscard]] bool has_reboot_name(std::wstring_view entry) noexcept {
    const std::wstring_view name = name_of(entry);
    if (name.size() < kRebootPrefix.size()) return false;
    for (std::size_t i = 0; i < kRebootPrefix.size(); ++i)
        if (ascii_upper(name[i]) != kRebootPrefix[i]) return false;
    return true;
}

// CreateProcessW expects the block sorted by name, ignoring case.
[[nodiscard]] bool sorts_after_home(std::wstring_view entry) noexcept {
    return std::ranges::lexicographical_compare(kHomeName, name_of(entry), {}, ascii_upper, ascii_upper);
}

}  // namespace

std::wstring engine_environment_block(std::span<const std::wstring> inherited, const DataRoot& root) {
    std::wstring block;
    bool home_due = root.overridden;
    const auto add_home = [&] {
        block += kHomeName;
        block += L'=';
        block += root.root.wstring();
        block += L'\0';
        home_due = false;
    };
    for (const std::wstring& entry : inherited) {
        if (entry.empty() || has_reboot_name(entry)) continue;
        if (home_due && sorts_after_home(entry)) add_home();
        block += entry;
        block += L'\0';
    }
    if (home_due) add_home();
    // An empty block still needs its two terminating NULs.
    if (block.empty()) block += L'\0';
    block += L'\0';
    return block;
}

}  // namespace reboot::os_windows::ipc
