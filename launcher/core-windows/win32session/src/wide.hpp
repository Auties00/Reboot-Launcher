#pragma once

#include <string>
#include <vector>

#include "reboot/contracts/winhost.hpp"

namespace reboot::os_windows::win32session {

using contracts::winhost::Bytes;

// Reinterprets a UTF-16LE byte buffer as wchar_t and drops a trailing NUL unit if present. An odd
// length drops its last byte; the callers only ever pass even buffers.
[[nodiscard]] std::wstring to_wide(const Bytes& utf16le);

// Joins the argv with CommandLineToArgvW's quoting rules so a path with spaces or quotes round
// trips. argv[0] is the exe.
[[nodiscard]] std::wstring build_command_line(const Bytes& exe_utf16, const std::vector<Bytes>& argv_utf16);

}  // namespace reboot::os_windows::win32session
