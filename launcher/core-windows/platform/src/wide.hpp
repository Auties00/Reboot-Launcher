#pragma once

#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::os_windows::platform {

using EnvVars = std::vector<std::pair<std::string, std::string>>;

[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);

// The \\?\ form of `path`, absolute and normalised, so MAX_PATH never applies. A drive root keeps
// its backslash, since \\?\C: names the volume device.
[[nodiscard]] std::wstring extended_path(const NativePath& path);

// `path` absolute and normalised with backslashes, for the shell APIs that reject \\?\.
[[nodiscard]] std::wstring shell_path(const NativePath& path);

// Quotes one argument so CommandLineToArgvW and the CRT give it back unchanged.
void append_argument(std::wstring& line, std::wstring_view arg);
// argv[0] is `exe`; `args` follow. An empty argument stays an empty quoted argument.
[[nodiscard]] std::wstring build_command_line(const NativePath& exe, std::span<const std::string> args);

// A CREATE_UNICODE_ENVIRONMENT block: `base` ("NAME=value" entries) with each of `overlay` set,
// names compared case-insensitively as Windows does, sorted, and double-NUL terminated.
[[nodiscard]] std::wstring build_environment_block(std::span<const std::wstring> base, const EnvVars& overlay);

// FILETIME ticks (100 ns since 1601) and back.
[[nodiscard]] std::chrono::system_clock::time_point from_filetime(u64 ticks) noexcept;
[[nodiscard]] u64 to_filetime(std::chrono::system_clock::time_point time) noexcept;

// Process creation times are compared at the microseconds runtime.json keeps.
[[nodiscard]] bool same_creation_time(std::chrono::system_clock::time_point a,
                                      std::chrono::system_clock::time_point b) noexcept;

}  // namespace rb::os_windows::platform
