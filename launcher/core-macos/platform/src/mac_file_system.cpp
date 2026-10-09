#include "darwin.hpp"

#include "reboot/os_macos/platform/mac_file_system.hpp"

#include <cerrno>

#include "full_fsync.hpp"
#include "reboot/posix/posix_error.hpp"

namespace reboot::os_macos::platform {

namespace {

[[nodiscard]] Result<void> sync_file(int fd) { return full_fsync(fd); }

[[nodiscard]] Result<void> sync_directory(int fd) {
    Result<void> synced = full_fsync(fd);
    // Some file systems (SMB, FUSE) cannot sync a directory; the rename itself stands.
    if (!synced && synced.error().os_error && synced.error().os_error->code == EINVAL) return {};
    return synced;
}

}  // namespace

MacFileSystem::MacFileSystem() noexcept : posix_(sync_file, sync_directory) {}

Result<void> MacFileSystem::atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) {
    return posix_.atomic_replace(target, bytes, keep_backup);
}

Result<std::vector<u8>> MacFileSystem::read_all(const NativePath& path) { return posix_.read_all(path); }

Result<ports::FileLock> MacFileSystem::lock_exclusive(const NativePath& path, bool wait) {
    return posix_.lock_exclusive(path, wait);
}

Result<void> MacFileSystem::restrict_to_owner(const NativePath& path) { return posix_.restrict_to_owner(path); }

Result<ports::HeldFile> MacFileSystem::open_deny_write(const NativePath& path) { return posix_.open_deny_write(path); }

Result<ports::FileRevision> MacFileSystem::revision(const NativePath& path) { return posix_.revision(path); }

Result<ports::SharedRead> MacFileSystem::read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) {
    return posix_.read_shared(path, offset, max_bytes);
}

Result<void> MacFileSystem::create_dirs_owner_only(const NativePath& path) {
    return posix_.create_dirs_owner_only(path);
}

Result<void> MacFileSystem::remove_tree(const NativePath& path) { return posix_.remove_tree(path); }

}  // namespace reboot::os_macos::platform
