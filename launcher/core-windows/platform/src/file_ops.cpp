#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "file_ops.hpp"

#include <array>
#include <thread>

#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

using namespace std::chrono_literals;

constexpr std::array<std::chrono::milliseconds, 8> kBackoff{10ms, 20ms, 40ms, 80ms, 160ms, 250ms, 250ms, 250ms};

}  // namespace

bool is_transient_share_error(DWORD error, bool access_denied_too) noexcept {
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_UNABLE_TO_REMOVE_REPLACED || (access_denied_too && error == ERROR_ACCESS_DENIED);
}

DWORD retry_transient(UniqueFunction<DWORD()> attempt, bool access_denied_too) {
    DWORD error = attempt();
    for (const auto delay : kBackoff) {
        if (error == 0 || !is_transient_share_error(error, access_denied_too)) return error;
        std::this_thread::sleep_for(delay);
        error = attempt();
    }
    return error;
}

Result<UniqueHandle> open_file(const NativePath& path, DWORD access, DWORD share, DWORD disposition, DWORD flags,
                               std::string_view call) {
    const std::wstring target = extended_path(path);
    UniqueHandle handle;
    const DWORD error = retry_transient([&]() -> DWORD {
        handle.reset(CreateFileW(target.c_str(), access, share, nullptr, disposition, flags, nullptr));
        return handle ? 0 : GetLastError();
    });
    if (error != 0) return std::unexpected(call_failed(call, error, path));
    return handle;
}

DWORD delete_by_handle(HANDLE handle) noexcept {
    FILE_DISPOSITION_INFO_EX posix{};
    posix.Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                  FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE;
    if (SetFileInformationByHandle(handle, FileDispositionInfoEx, &posix, sizeof posix) != 0) return 0;
    const DWORD error = GetLastError();
    // FAT, network shares and older file systems take only the classic disposition.
    if (error != ERROR_INVALID_PARAMETER && error != ERROR_NOT_SUPPORTED && error != ERROR_INVALID_FUNCTION)
        return error;
    FILE_BASIC_INFO basic{};
    if (GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof basic) != 0 &&
        (basic.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
        basic.FileAttributes &= ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY);
        if (basic.FileAttributes == 0) basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (SetFileInformationByHandle(handle, FileBasicInfo, &basic, sizeof basic) == 0) return GetLastError();
    }
    FILE_DISPOSITION_INFO classic{};
    classic.DeleteFile = TRUE;
    if (SetFileInformationByHandle(handle, FileDispositionInfo, &classic, sizeof classic) == 0) return GetLastError();
    return 0;
}

std::chrono::system_clock::time_point to_time_point(const FILETIME& time) noexcept {
    return from_filetime((static_cast<u64>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
}

}  // namespace rb::os_windows::platform
