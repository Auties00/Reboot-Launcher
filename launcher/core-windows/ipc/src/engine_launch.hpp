#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_windows::ipc {

// Task Scheduler names are machine-wide, hence the SID. Must equal
// os_windows::platform::engine_task_name, which registers the task.
[[nodiscard]] std::string engine_task_name(std::string_view user_sid);

// `"<engine_exe>" run --origin=on-demand`; a Windows path cannot hold a quote.
[[nodiscard]] std::wstring engine_command_line(const NativePath& engine_exe);

// The "NAME=value" entries of a GetEnvironmentStringsW block, which ends at an empty entry.
[[nodiscard]] std::vector<std::wstring> environment_entries(const wchar_t* block);

// Whether a task's exec action path, quoted or not and already expanded, names `engine_exe`,
// ignoring case as Windows does.
[[nodiscard]] bool action_runs(std::wstring_view action_path, const NativePath& engine_exe);

// A CallerContext os_session: a decimal Windows session id.
[[nodiscard]] std::optional<u32> parse_session_id(std::string_view os_session);

}  // namespace reboot::os_windows::ipc
