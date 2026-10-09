#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "env_overlay.hpp"

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

namespace rb::os_windows::winhost {
namespace {

using contracts::winhost::Bytes;

// The name of NAME=VALUE; a leading '=' belongs to the name, as in the per-drive "=C:=C:\x".
std::wstring_view name_of(std::wstring_view entry) noexcept { return entry.substr(0, entry.find(L'=', 1)); }

int compare_names(std::wstring_view a, std::wstring_view b) noexcept {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
}

void split_block(std::wstring_view block, std::vector<std::wstring_view>& out) {
    while (!block.empty()) {
        const std::size_t end = block.find(L'\0');
        const std::wstring_view entry = block.substr(0, end);
        if (entry.empty()) break;
        out.push_back(entry);
        if (end == std::wstring_view::npos) break;
        block.remove_prefix(end + 1);
    }
}

}  // namespace

Bytes overlay_environment(const wchar_t* base, const Bytes& overlay) {
    std::vector<std::wstring_view> entries;
    for (const wchar_t* p = base; p != nullptr && *p != L'\0'; p += std::wcslen(p) + 1) entries.emplace_back(p);

    std::wstring overlay_units(overlay.size() / 2, L'\0');
    for (std::size_t i = 0; i < overlay_units.size(); ++i)
        overlay_units[i] = static_cast<wchar_t>(overlay[2 * i] | (overlay[2 * i + 1] << 8));
    std::vector<std::wstring_view> layered;
    split_block(overlay_units, layered);
    for (const std::wstring_view entry : layered) {
        const auto same = std::ranges::find_if(
            entries, [&](std::wstring_view existing) { return compare_names(name_of(existing), name_of(entry)) == CSTR_EQUAL; });
        if (same != entries.end()) *same = entry;
        else entries.push_back(entry);
    }
    std::ranges::stable_sort(entries, [](std::wstring_view a, std::wstring_view b) {
        return compare_names(name_of(a), name_of(b)) == CSTR_LESS_THAN;
    });

    std::size_t units = 1;
    for (const std::wstring_view entry : entries) units += entry.size() + 1;
    Bytes block;
    block.reserve(units * 2);
    const auto put = [&](wchar_t unit) {
        block.push_back(static_cast<u8>(unit & 0xFF));
        block.push_back(static_cast<u8>((unit >> 8) & 0xFF));
    };
    for (const std::wstring_view entry : entries) {
        for (const wchar_t unit : entry) put(unit);
        put(L'\0');
    }
    put(L'\0');
    if (!overlay_units.empty()) SecureZeroMemory(overlay_units.data(), overlay_units.size() * sizeof(wchar_t));
    return block;
}

Bytes overlay_own_environment(const Bytes& overlay) {
    wchar_t* own = GetEnvironmentStringsW();
    Bytes block = overlay_environment(own, overlay);
    if (own != nullptr) FreeEnvironmentStringsW(own);
    return block;
}

}  // namespace rb::os_windows::winhost
