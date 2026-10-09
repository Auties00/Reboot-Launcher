#pragma once

#include <string>
#include <string_view>

namespace reboot::os_linux::platform {

struct OsRelease {
    std::string name;
    std::string version_id;
    std::string build_id;
};

// os-release(5): KEY=value lines, values optionally in single or double quotes with backslash
// escapes; comments and unknown keys are ignored.
[[nodiscard]] OsRelease parse_os_release(std::string_view text);

// Whether a NUL-separated environment block, as /proc/<pid>/environ holds it, sets `name`.
[[nodiscard]] bool environ_block_has(std::string_view block, std::string_view name) noexcept;

}  // namespace reboot::os_linux::platform
