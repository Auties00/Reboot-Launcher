#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_file_system.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <string>

#include "file_ops.hpp"
#include "messages.hpp"
#include "process_token.hpp"
#include "reboot/foundation/random.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr DWORD kChunk = 1u << 20;

[[nodiscard]] Result<void> write_all(HANDLE file, std::span<const u8> bytes, const NativePath& shown) {
    while (!bytes.empty()) {
        const DWORD chunk = bytes.size() > kChunk ? kChunk : static_cast<DWORD>(bytes.size());
        DWORD written = 0;
        if (WriteFile(file, bytes.data(), chunk, &written, nullptr) == 0)
            return std::unexpected(call_failed("WriteFile", GetLastError(), shown));
        bytes = bytes.subspan(written);
    }
    return {};
}

[[nodiscard]] NativePath temp_beside(const NativePath& target) {
    OsRandom random;
    NativePath temp = target;
    temp += "." + random_token_hex(random, 8) + ".tmp";
    return temp;
}

// Ends a lock when the handle closes; the unlock only makes the release immediate.
class LockFileHandle final : public ports::FileLock::Handle {
public:
    explicit LockFileHandle(UniqueHandle file) noexcept : file_(std::move(file)) {}
    ~LockFileHandle() override {
        OVERLAPPED whole{};
        UnlockFileEx(file_.get(), 0, MAXDWORD, MAXDWORD, &whole);
    }

private:
    UniqueHandle file_;
};

class DenyWriteFile final : public ports::HeldFile::Handle {
public:
    DenyWriteFile(UniqueHandle file, NativePath path) noexcept : file_(std::move(file)), path_(std::move(path)) {}

    Result<std::size_t> read(std::span<u8> out) override {
        const DWORD want = out.size() > kChunk ? kChunk : static_cast<DWORD>(out.size());
        DWORD got = 0;
        if (ReadFile(file_.get(), out.data(), want, &got, nullptr) == 0)
            return std::unexpected(call_failed("ReadFile", GetLastError(), path_));
        return got;
    }

private:
    UniqueHandle file_;
    NativePath path_;
};

// The names in an open directory, enumerated through its handle so a swapped-in link is never read.
[[nodiscard]] Result<std::vector<std::wstring>> list_names(HANDLE directory, const NativePath& shown) {
    std::vector<std::wstring> names;
    std::vector<u64> buffer(64 * 1024 / sizeof(u64));
    auto info_class = FileFullDirectoryRestartInfo;
    for (;;) {
        if (GetFileInformationByHandleEx(directory, info_class, buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(u64))) ==
            0) {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_FILES) return names;
            return std::unexpected(call_failed("GetFileInformationByHandleEx", error, shown));
        }
        info_class = FileFullDirectoryInfo;
        const auto* bytes = reinterpret_cast<const std::byte*>(buffer.data());
        for (;;) {
            const auto* entry = reinterpret_cast<const FILE_FULL_DIR_INFO*>(bytes);
            const std::wstring_view name(entry->FileName, entry->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") names.emplace_back(name);
            if (entry->NextEntryOffset == 0) break;
            bytes += entry->NextEntryOffset;
        }
    }
}

[[nodiscard]] Result<void> remove_entry(const NativePath& path) {
    constexpr DWORD kAccess = DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | FILE_LIST_DIRECTORY | SYNCHRONIZE;
    auto opened = open_file(path, kAccess, kShareAll, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
    if (!opened) {
        if (opened.error().kind == ErrorKind::NotFound) return {};
        return std::unexpected(std::move(opened.error()));
    }
    FILE_BASIC_INFO basic{};
    if (GetFileInformationByHandleEx(opened->get(), FileBasicInfo, &basic, sizeof basic) == 0)
        return std::unexpected(call_failed("GetFileInformationByHandleEx", GetLastError(), path));
    const bool link = (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    if ((basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && !link) {
        auto names = list_names(opened->get(), path);
        if (!names) return std::unexpected(std::move(names.error()));
        for (const std::wstring& name : *names)
            if (auto removed = remove_entry(path / name); !removed) return removed;
    }
    // A child another process still holds without POSIX semantics keeps the directory busy briefly.
    const DWORD error = retry_transient([&]() -> DWORD {
        const DWORD result = delete_by_handle(opened->get());
        return result == ERROR_DIR_NOT_EMPTY ? ERROR_SHARING_VIOLATION : result;
    });
    if (error != 0) return std::unexpected(call_failed("SetFileInformationByHandle", error, path));
    return {};
}

[[nodiscard]] Result<DWORD> attributes_of(const NativePath& path) {
    const std::wstring target = extended_path(path);
    const DWORD attributes = GetFileAttributesW(target.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return std::unexpected(call_failed("GetFileAttributesW", GetLastError(), path));
    return attributes;
}

}  // namespace

Result<void> WindowsFileSystem::atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) {
    const NativePath temp = temp_beside(target);
    {
        auto file = open_file(temp, GENERIC_WRITE | DELETE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL);
        if (!file) return std::unexpected(std::move(file.error()));
        auto written = write_all(file->get(), bytes, target);
        if (written && FlushFileBuffers(file->get()) == 0) written = std::unexpected(call_failed("FlushFileBuffers", GetLastError(), target));
        if (!written) {
            (void)delete_by_handle(file->get());
            return written;
        }
    }

    const std::wstring wide_target = extended_path(target);
    const std::wstring wide_temp = extended_path(temp);
    NativePath backup = target;
    backup += ".bak";
    const std::wstring wide_backup = extended_path(backup);
    const DWORD error = retry_transient(
        [&]() -> DWORD {
            if (ReplaceFileW(wide_target.c_str(), wide_temp.c_str(), keep_backup ? wide_backup.c_str() : nullptr,
                             REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, nullptr, nullptr) != 0)
                return 0;
            const DWORD replace_error = GetLastError();
            // No previous file, or the replacement kept its own name after the old one moved aside.
            if (replace_error != ERROR_FILE_NOT_FOUND && replace_error != ERROR_UNABLE_TO_MOVE_REPLACEMENT &&
                replace_error != ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
                return replace_error;
            if (MoveFileExW(wide_temp.c_str(), wide_target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
                return 0;
            return GetLastError();
        },
        true);
    if (error != 0) {
        DeleteFileW(wide_temp.c_str());
        return std::unexpected(call_failed("ReplaceFileW", error, target));
    }
    return {};
}

Result<std::vector<u8>> WindowsFileSystem::read_all(const NativePath& path) {
    auto file = open_file(path, GENERIC_READ, kShareAll, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
    if (!file) return std::unexpected(std::move(file.error()));
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file->get(), &size) == 0) return std::unexpected(call_failed("GetFileSizeEx", GetLastError(), path));
    std::vector<u8> bytes;
    bytes.reserve(static_cast<std::size_t>(size.QuadPart));
    std::array<u8, 64 * 1024> chunk{};
    for (;;) {
        DWORD got = 0;
        if (ReadFile(file->get(), chunk.data(), static_cast<DWORD>(chunk.size()), &got, nullptr) == 0)
            return std::unexpected(call_failed("ReadFile", GetLastError(), path));
        if (got == 0) return bytes;
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + got);
    }
}

Result<ports::FileLock> WindowsFileSystem::lock_exclusive(const NativePath& path, bool wait) {
    auto file = open_file(path, GENERIC_READ | GENERIC_WRITE, kShareAll, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (!file) return std::unexpected(std::move(file.error()));
    OVERLAPPED whole{};
    const DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | (wait ? 0 : LOCKFILE_FAIL_IMMEDIATELY);
    if (LockFileEx(file->get(), flags, 0, MAXDWORD, MAXDWORD, &whole) == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING)
            return make_diag(ErrorDomain::Platform, kLockBusy).arg("path", path).kind(ErrorKind::Conflict).retryable().fail();
        return std::unexpected(call_failed("LockFileEx", error, path));
    }
    return ports::FileLock(std::make_unique<LockFileHandle>(std::move(*file)));
}

Result<void> WindowsFileSystem::restrict_to_owner(const NativePath& path) {
    auto attributes = attributes_of(path);
    if (!attributes) return std::unexpected(std::move(attributes.error()));
    auto dacl = OwnerOnlyDacl::create((*attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    if (!dacl) return std::unexpected(std::move(dacl.error()));
    std::wstring target = extended_path(path);
    const DWORD error = SetNamedSecurityInfoW(target.data(), SE_FILE_OBJECT,
                                              DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                              nullptr, dacl->acl(), nullptr);
    if (error != ERROR_SUCCESS) return std::unexpected(call_failed("SetNamedSecurityInfoW", error, path));
    return {};
}

Result<ports::HeldFile> WindowsFileSystem::open_deny_write(const NativePath& path) {
    auto file = open_file(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
    if (!file) return std::unexpected(std::move(file.error()));
    return ports::HeldFile(std::make_unique<DenyWriteFile>(std::move(*file), path));
}

Result<ports::FileRevision> WindowsFileSystem::revision(const NativePath& path) {
    auto file = open_file(path, FILE_READ_ATTRIBUTES, kShareAll, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    if (!file) return std::unexpected(std::move(file.error()));
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(file->get(), &info) == 0)
        return std::unexpected(call_failed("GetFileInformationByHandle", GetLastError(), path));
    ports::FileRevision revision;
    revision.size = (static_cast<u64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    revision.mtime = to_time_point(info.ftLastWriteTime);
    revision.file_id = (static_cast<u64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    return revision;
}

Result<ports::SharedRead> WindowsFileSystem::read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) {
    auto file = open_file(path, GENERIC_READ, kShareAll, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    if (!file) return std::unexpected(std::move(file.error()));
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(file->get(), &info) == 0)
        return std::unexpected(call_failed("GetFileInformationByHandle", GetLastError(), path));
    ports::SharedRead read;
    read.revision.size = (static_cast<u64>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    read.revision.mtime = to_time_point(info.ftLastWriteTime);
    read.revision.file_id = (static_cast<u64>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    if (offset >= read.revision.size) return read;
    // The file may still grow while it is read, so the bound is max_bytes, not the size seen.
    read.bytes.resize(max_bytes);
    std::size_t filled = 0;
    while (filled < max_bytes) {
        const u64 at = offset + filled;
        OVERLAPPED position{};
        position.Offset = static_cast<DWORD>(at);
        position.OffsetHigh = static_cast<DWORD>(at >> 32);
        const std::size_t want = std::min<std::size_t>(max_bytes - filled, kChunk);
        DWORD got = 0;
        if (ReadFile(file->get(), read.bytes.data() + filled, static_cast<DWORD>(want), &got, &position) == 0) {
            const DWORD error = GetLastError();
            if (error == ERROR_HANDLE_EOF) break;
            return std::unexpected(call_failed("ReadFile", error, path));
        }
        if (got == 0) break;
        filled += got;
    }
    read.bytes.resize(filled);
    return read;
}

Result<void> WindowsFileSystem::create_dirs_owner_only(const NativePath& path) {
    const NativePath absolute = NativePath(shell_path(path));
    auto dacl = OwnerOnlyDacl::create(true);
    if (!dacl) return std::unexpected(std::move(dacl.error()));
    NativePath current = absolute.root_path();
    for (const NativePath& part : absolute.relative_path()) {
        if (part.empty()) continue;
        current /= part;
        const std::wstring wide = extended_path(current);
        const DWORD attributes = GetFileAttributesW(wide.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                return std::unexpected(call_failed("CreateDirectoryW", ERROR_DIRECTORY, path));
            continue;
        }
        if (CreateDirectoryW(wide.c_str(), dacl->attributes()) == 0) {
            const DWORD error = GetLastError();
            // Another process made it between the check and the create.
            if (error == ERROR_ALREADY_EXISTS) continue;
            return std::unexpected(call_failed("CreateDirectoryW", error, path));
        }
    }
    return {};
}

Result<void> WindowsFileSystem::remove_tree(const NativePath& path) { return remove_entry(path); }

}  // namespace rb::os_windows::platform
