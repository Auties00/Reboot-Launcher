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
// trips. argv[0] is the exe. The argv may hold a password, so no intermediate copy is left behind.
[[nodiscard]] std::wstring build_command_line(const Bytes& exe_utf16, const std::vector<Bytes>& argv_utf16);

// Pads a UTF-16LE environment block to an even length ending in two NUL units, which is where
// CreateProcessW stops reading it.
void terminate_env_block(Bytes& block);

// Zeroes the contents in a way the optimiser keeps, for buffers that held argv or environment.
void wipe(std::wstring& text) noexcept;
void wipe(Bytes& bytes) noexcept;

}  // namespace reboot::os_windows::win32session
