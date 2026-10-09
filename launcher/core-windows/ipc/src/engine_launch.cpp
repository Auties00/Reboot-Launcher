#include "engine_launch.hpp"

#include <charconv>
#include <cwchar>

#include "reboot/foundation/paths.hpp"

namespace rb::os_windows::ipc {

std::string engine_task_name(std::string_view user_sid) {
    std::string name = "Reboot Launcher Engine ";
    name += user_sid;
    return name;
}

std::wstring engine_command_line(const NativePath& engine_exe) {
    std::wstring line = L"\"";
    line += engine_exe.wstring();
    line += L"\" run --origin=on-demand";
    return line;
}

std::vector<std::wstring> environment_entries(const wchar_t* block) {
    std::vector<std::wstring> entries;
    if (block == nullptr) return entries;
    while (*block != L'\0') {
        const std::size_t length = std::wcslen(block);
        entries.emplace_back(block, length);
        block += length + 1;
    }
    return entries;
}

bool action_runs(std::wstring_view action_path, const NativePath& engine_exe) {
    if (action_path.size() >= 2 && action_path.front() == L'"' && action_path.back() == L'"')
        action_path = action_path.substr(1, action_path.size() - 2);
    const NativePath action{std::wstring(action_path)};
    if (!action.is_absolute() || !engine_exe.is_absolute()) return false;
    return is_inside(action, engine_exe) && is_inside(engine_exe, action);
}

std::optional<u32> parse_session_id(std::string_view os_session) {
    u32 id = 0;
    const auto [end, error] = std::from_chars(os_session.data(), os_session.data() + os_session.size(), id);
    if (error != std::errc{} || end != os_session.data() + os_session.size() || os_session.empty()) return std::nullopt;
    return id;
}

}  // namespace rb::os_windows::ipc
