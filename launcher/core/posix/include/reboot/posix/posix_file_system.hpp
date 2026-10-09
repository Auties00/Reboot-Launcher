#pragma once

#include <span>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::posix {

// Covers no capability ids; the IFileSystem adapter shared by core-macos and core-linux.
class PosixFileSystem final : public ports::IFileSystem {
public:
    using SyncFd = UniqueFunction<Result<void>(int fd)>;

    // Both hooks are required; atomic_replace fails with internal.bug rather than call an unset one.
    // `sync_file` flushes a written file to stable storage: F_FULLFSYNC on macOS, fsync on Linux.
    // `sync_directory` makes a rename durable on every POSIX system: F_FULLFSYNC of the directory
    // on macOS, fsync on Linux.
    PosixFileSystem(SyncFd sync_file, SyncFd sync_directory) noexcept
        : sync_file_(std::move(sync_file)), sync_directory_(std::move(sync_directory)) {}

    // A 0600 temp file beside the target, sync_file, a hard-linked <target>.bak when asked and a
    // previous file exists, rename over the target, then sync_directory.
    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override;
    Result<std::vector<u8>> read_all(const NativePath& path) override;
    // flock on a 0600 lock file, created when missing. The lock belongs to the open file
    // description, so another fd of the same file closing elsewhere keeps it.
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override;
    // 0600 for a file, 0700 for a directory; never follows a link.
    Result<void> restrict_to_owner(const NativePath& path) override;
    // POSIX cannot deny writers: this holds an O_RDONLY fd, and a read fails with
    // posix.held_file_changed once the path names another inode or the held file's size or
    // mtime moved since open. An in-place write keeping both goes unseen, so this is no security
    // boundary: payload verification and injection staging rely on the 0700 data directory.
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override;
    Result<ports::FileRevision> revision(const NativePath& path) override;
    // pread on an O_RDONLY fd, which never blocks a writer, rename or unlink.
    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override;
    // Missing components are created 0700; existing ones are left as they are.
    Result<void> create_dirs_owner_only(const NativePath& path) override;
    // A missing path is success.
    Result<void> remove_tree(const NativePath& path) override;

private:
    SyncFd sync_file_;
    SyncFd sync_directory_;
};

// Covers no capability ids; IFileRevisionReader over stat, for reboot_client.
class PosixFileRevisionReader final : public ports::IFileRevisionReader {
public:
    Result<ports::FileRevision> revision(const NativePath& path) override;
};

}  // namespace reboot::posix
