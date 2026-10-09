#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::ports {

// Releases the lock on destruction.
class FileLock {
public:
    class Handle {
    public:
        virtual ~Handle() = default;
    };

    FileLock() = default;
    explicit FileLock(std::unique_ptr<Handle> handle) : handle_(std::move(handle)) {}

    [[nodiscard]] bool held() const noexcept { return handle_ != nullptr; }
    void release() noexcept { handle_.reset(); }

private:
    std::unique_ptr<Handle> handle_;
};

// Keeps a file open while denying other writers and deleters.
class HeldFile {
public:
    class Handle {
    public:
        virtual ~Handle() = default;
        virtual Result<std::size_t> read(std::span<u8> out) = 0;
    };

    HeldFile() = default;
    explicit HeldFile(std::unique_ptr<Handle> handle) : handle_(std::move(handle)) {}

    // Reads sequentially; 0 means end of file.
    Result<std::size_t> read(std::span<u8> out) { return handle_->read(out); }

private:
    std::unique_ptr<Handle> handle_;
};

struct FileRevision {
    u64 size = 0;
    std::chrono::system_clock::time_point mtime;
    u64 file_id = 0;  // inode on POSIX, file index on Windows

    bool operator==(const FileRevision&) const = default;
};

// `revision` is the opened file's, so its file_id names the file the bytes came from.
struct SharedRead {
    FileRevision revision;
    std::vector<u8> bytes;
};

// The part of IFileSystem reboot_client needs, for the update marker. Blocking.
class IFileRevisionReader {
public:
    virtual ~IFileRevisionReader() = default;

    virtual Result<FileRevision> revision(const NativePath& path) = 0;
};

// Blocking; called from the WorkerPool.
class IFileSystem : public IFileRevisionReader {
public:
    // Temp file, full fsync, then replace. `keep_backup` leaves the previous file as <target>.bak.
    virtual Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) = 0;
    virtual Result<std::vector<u8>> read_all(const NativePath& path) = 0;
    // Without `wait`, a lock held elsewhere fails at once.
    virtual Result<FileLock> lock_exclusive(const NativePath& path, bool wait) = 0;
    virtual Result<void> restrict_to_owner(const NativePath& path) = 0;
    virtual Result<HeldFile> open_deny_write(const NativePath& path) = 0;
    // Up to `max_bytes` from `offset`, through a handle that never stops another process writing,
    // renaming or deleting the file; past the end it reads nothing.
    virtual Result<SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) = 0;
    virtual Result<void> create_dirs_owner_only(const NativePath& path) = 0;
    // Never follows links.
    virtual Result<void> remove_tree(const NativePath& path) = 0;
};

enum class FileChangeKind : u8 { Created, Removed, Modified, Renamed };

struct FileChange {
    NativePath path;
    FileChangeKind kind{};
};

// Stops watching on destruction.
class WatchHandle {
public:
    class Handle {
    public:
        virtual ~Handle() = default;
    };

    WatchHandle() = default;
    explicit WatchHandle(std::unique_ptr<Handle> handle) : handle_(std::move(handle)) {}

private:
    std::unique_ptr<Handle> handle_;
};

class IFileWatcher {
public:
    virtual ~IFileWatcher() = default;
    // `on_change` runs on the watcher's thread; callers post to the strand.
    virtual Result<WatchHandle> watch(const NativePath& dir, UniqueFunction<void(FileChange)> on_change) = 0;
};

struct VolumeInfo {
    NativePath mount;
    std::string label;
    std::string fs_type;
    u64 free_bytes = 0;
    u64 total_bytes = 0;
    bool read_only = false;
    bool removable = false;
    bool network = false;
};

class IDiskInfo {
public:
    virtual ~IDiskInfo() = default;
    virtual Result<std::vector<VolumeInfo>> volumes() = 0;
    virtual Result<VolumeInfo> volume_of(const NativePath& path) = 0;
};

}  // namespace rb::ports
