#pragma once

#include <string>
#include <string_view>

namespace rb::os_windows::ipc {

[[nodiscard]] std::wstring to_wide(std::string_view utf8);
[[nodiscard]] std::string to_utf8(std::wstring_view wide);

}  // namespace rb::os_windows::ipc
