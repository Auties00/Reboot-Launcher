#pragma once

#include <chrono>
#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "unique_handle.hpp"
#include "win32.hpp"

namespace rb::os_windows::platform {

// Sharing and lock violations: what an AV scanner or the indexer holding a fresh file open produces
// for a moment. A rename over such a file can also be denied access, so a replace may count that too.
[[nodiscard]] bool is_transient_share_error(DWORD error, bool access_denied_too = false) noexcept;

// Runs `attempt` (returning a Win32 error, 0 on success) until it succeeds, fails otherwise, or
// about a second of transient failures passed; returns the last error.
[[nodiscard]] DWORD retry_transient(UniqueFunction<DWORD()> attempt, bool access_denied_too = false);

// CreateFileW on the \\?\ form of `path`, retrying transient sharing failures.
[[nodiscard]] Result<UniqueHandle> open_file(const NativePath& path, DWORD access, DWORD share, DWORD disposition,
                                             DWORD flags, std::string_view call = "CreateFileW");

// Marks an open handle (opened with DELETE) for deletion, unlinking the name at once where the
// volume supports POSIX semantics and clearing a read-only attribute first. Returns the Win32 error.
[[nodiscard]] DWORD delete_by_handle(HANDLE handle) noexcept;

[[nodiscard]] std::chrono::system_clock::time_point to_time_point(const FILETIME& time) noexcept;

}  // namespace rb::os_windows::platform
