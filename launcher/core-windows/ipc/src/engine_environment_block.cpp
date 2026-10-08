#include "engine_environment_block.hpp"

#include <string_view>

namespace reboot::os_windows::ipc {
namespace {

constexpr std::wstring_view kRebootPrefix = L"REBOOT_";

[[nodiscard]] wchar_t ascii_upper(wchar_t c) noexcept {
    return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - (L'a' - L'A')) : c;
}

// Hidden per-drive entries such as "=C:=C:\dir" start with '=', so the name ends at a later one.
[[nodiscard]] bool has_reboot_name(std::wstring_view entry) noexcept {
    const std::wstring_view name = entry.substr(0, entry.find(L'=', 1));
    if (name.size() < kRebootPrefix.size()) return false;
    for (std::size_t i = 0; i < kRebootPrefix.size(); ++i)
        if (ascii_upper(name[i]) != kRebootPrefix[i]) return false;
    return true;
}

}  // namespace

std::wstring engine_environment_block(std::span<const std::wstring> inherited, const DataRoot& root) {
    std::wstring block;
    for (const std::wstring& entry : inherited) {
        if (entry.empty() || has_reboot_name(entry)) continue;
        block += entry;
        block += L'\0';
    }
    if (root.overridden) {
        block += L"REBOOT_LAUNCHER_HOME=";
        block += root.root.wstring();
        block += L'\0';
    }
    // An empty block still needs its two terminating NULs.
    if (block.empty()) block += L'\0';
    block += L'\0';
    return block;
}

}  // namespace reboot::os_windows::ipc
