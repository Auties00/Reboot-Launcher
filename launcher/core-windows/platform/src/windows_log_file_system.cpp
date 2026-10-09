#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_log_file_system.hpp"

#include <memory>
#include <string>

#include "file_ops.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

class AppendFile final : public ports::LogFile::Handle {
public:
    AppendFile(UniqueHandle file, NativePath path) noexcept : file_(std::move(file)), path_(std::move(path)) {}

    Result<void> append(std::span<const u8> bytes) override {
        while (!bytes.empty()) {
            const DWORD chunk = bytes.size() > (1u << 20) ? (1u << 20) : static_cast<DWORD>(bytes.size());
            DWORD written = 0;
            if (WriteFile(file_.get(), bytes.data(), chunk, &written, nullptr) == 0)
                return std::unexpected(call_failed("WriteFile", GetLastError(), path_));
            bytes = bytes.subspan(written);
        }
        return {};
    }

    Result<void> flush() override {
        if (FlushFileBuffers(file_.get()) == 0) return std::unexpected(call_failed("FlushFileBuffers", GetLastError(), path_));
        return {};
    }

private:
    UniqueHandle file_;
    NativePath path_;
};

}  // namespace

Result<void> WindowsLogFileSystem::create_directories(const NativePath& dir) {
    const NativePath absolute = NativePath(shell_path(dir));
    NativePath current = absolute.root_path();
    for (const NativePath& part : absolute.relative_path()) {
        if (part.empty()) continue;
        current /= part;
        const std::wstring wide = extended_path(current);
        if (CreateDirectoryW(wide.c_str(), nullptr) != 0) continue;
        const DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) return std::unexpected(call_failed("CreateDirectoryW", error, dir));
        const DWORD attributes = GetFileAttributesW(wide.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return std::unexpected(call_failed("CreateDirectoryW", ERROR_DIRECTORY, dir));
    }
    return {};
}

Result<ports::LogFile> WindowsLogFileSystem::open_append(const NativePath& path) {
    auto file = open_file(path, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, kShareAll, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
    if (!file) return std::unexpected(std::move(file.error()));
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(file->get(), &info) == 0)
        return std::unexpected(call_failed("GetFileInformationByHandle", GetLastError(), path));
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
        return std::unexpected(call_failed("CreateFileW", ERROR_CANT_ACCESS_FILE, path));
    const u64 size = (static_cast<u64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    return ports::LogFile(std::make_unique<AppendFile>(std::move(*file), path), size);
}

Result<std::vector<ports::LogDirEntry>> WindowsLogFileSystem::list(const NativePath& dir) {
    const std::wstring pattern = extended_path(dir) + L"\\*";
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr,
                                     FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE) return std::unexpected(call_failed("FindFirstFileExW", GetLastError(), dir));
    std::vector<ports::LogDirEntry> entries;
    do {
        if ((found.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) continue;
        ports::LogDirEntry entry;
        entry.path = dir / found.cFileName;
        entry.size = (static_cast<u64>(found.nFileSizeHigh) << 32) | found.nFileSizeLow;
        entry.modified = to_time_point(found.ftLastWriteTime);
        entries.push_back(std::move(entry));
    } while (FindNextFileW(search, &found) != 0);
    const DWORD error = GetLastError();
    FindClose(search);
    if (error != ERROR_NO_MORE_FILES) return std::unexpected(call_failed("FindNextFileW", error, dir));
    return entries;
}

Result<void> WindowsLogFileSystem::remove(const NativePath& path) {
    auto file = open_file(path, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, kShareAll, OPEN_EXISTING,
                          FILE_FLAG_OPEN_REPARSE_POINT);
    if (!file) return std::unexpected(std::move(file.error()));
    if (const DWORD error = delete_by_handle(file->get()); error != 0)
        return std::unexpected(call_failed("SetFileInformationByHandle", error, path));
    return {};
}

}  // namespace rb::os_windows::platform
