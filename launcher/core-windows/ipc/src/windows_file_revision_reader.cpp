#include "reboot/os_windows/ipc/windows_file_revision_reader.hpp"

#include "win32.hpp"

#include <chrono>

#include "unique_handle.hpp"
#include "win32_errors.hpp"

namespace reboot::os_windows::ipc {
namespace {

// FILETIME ticks (100 ns) from 1601-01-01 to 1970-01-01.
constexpr i64 kUnixEpochTicks = 116'444'736'000'000'000;

[[nodiscard]] std::chrono::system_clock::time_point to_time_point(const FILETIME& time) noexcept {
    const auto ticks = static_cast<i64>((static_cast<u64>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
    const std::chrono::duration<i64, std::ratio<1, 10'000'000>> since_unix{ticks - kUnixEpochTicks};
    return std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(since_unix)};
}

}  // namespace

Result<ports::FileRevision> WindowsFileRevisionReader::revision(const NativePath& path) {
    // Attributes only, sharing everything, so a writer of the file is never blocked.
    const UniqueHandle file{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (!file) return std::unexpected(call_failed_on_path("CreateFileW", path, GetLastError()));
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(file.get(), &info) == 0)
        return std::unexpected(call_failed_on_path("GetFileInformationByHandle", path, GetLastError()));
    return ports::FileRevision{.size = (static_cast<u64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow,
                               .mtime = to_time_point(info.ftLastWriteTime),
                               .file_id = (static_cast<u64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
}

}  // namespace reboot::os_windows::ipc
