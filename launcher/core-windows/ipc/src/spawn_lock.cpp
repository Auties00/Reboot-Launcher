#include "spawn_lock.hpp"

#include <filesystem>
#include <system_error>
#include <utility>

#include "messages.hpp"
#include "win32.hpp"
#include "win32_errors.hpp"

namespace rb::os_windows::ipc {

Result<SpawnLock> SpawnLock::acquire(const NativePath& path, std::chrono::milliseconds wait) {
    std::error_code created;
    std::filesystem::create_directories(path.parent_path(), created);
    if (created)
        return std::unexpected(call_failed_on_path("CreateDirectoryW", path.parent_path(), static_cast<DWORD>(created.value())));

    UniqueHandle file{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
    if (!file) return std::unexpected(call_failed_on_path("CreateFileW", path, GetLastError()));
    const UniqueHandle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event) return std::unexpected(call_failed("CreateEventW"));

    OVERLAPPED overlapped{};
    overlapped.hEvent = event.get();
    if (LockFileEx(file.get(), LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped)) return SpawnLock{std::move(file)};
    if (const DWORD error = GetLastError(); error != ERROR_IO_PENDING)
        return std::unexpected(call_failed_on_path("LockFileEx", path, error));

    DWORD transferred = 0;
    if (WaitForSingleObject(event.get(), static_cast<DWORD>(wait.count())) == WAIT_OBJECT_0) {
        if (!GetOverlappedResult(file.get(), &overlapped, &transferred, FALSE))
            return std::unexpected(call_failed_on_path("LockFileEx", path, GetLastError()));
        return SpawnLock{std::move(file)};
    }
    CancelIoEx(file.get(), &overlapped);
    // The lock may have been granted just before the cancel landed.
    if (GetOverlappedResult(file.get(), &overlapped, &transferred, TRUE)) return SpawnLock{std::move(file)};
    return std::unexpected(make_diag(ErrorDomain::Platform, kSpawnLockTimedOut).arg("path", path).arg("deadline", wait).build());
}

}  // namespace rb::os_windows::ipc
