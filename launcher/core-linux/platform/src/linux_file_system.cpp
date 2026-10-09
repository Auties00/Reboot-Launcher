#include "reboot/os_linux/platform/linux_file_system.hpp"

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <unistd.h>
#include <utility>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_linux::platform {

namespace {

[[nodiscard]] Result<void> sync_file(int fd) {
    while (::fsync(fd) != 0) {
        if (errno != EINTR) return std::unexpected(posix::call_failed("fsync", errno));
    }
    return {};
}

[[nodiscard]] Result<void> sync_directory(int fd) {
    while (::fsync(fd) != 0) {
        if (errno == EINTR) continue;
        // Some file systems (and FUSE mounts) cannot sync a directory; the rename itself stands.
        if (errno == EINVAL) return {};
        return std::unexpected(posix::call_failed("fsync", errno));
    }
    return {};
}

// Closing the fd drops the open file description and with it the OFD lock.
class OfdLockHandle final : public ports::FileLock::Handle {
public:
    explicit OfdLockHandle(posix::UniqueFd fd) noexcept : fd_(std::move(fd)) {}

private:
    posix::UniqueFd fd_;
};

}  // namespace

LinuxFileSystem::LinuxFileSystem() noexcept : posix_(sync_file, sync_directory) {}

Result<void> LinuxFileSystem::atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) {
    return posix_.atomic_replace(target, bytes, keep_backup);
}

Result<std::vector<u8>> LinuxFileSystem::read_all(const NativePath& path) { return posix_.read_all(path); }

Result<ports::FileLock> LinuxFileSystem::lock_exclusive(const NativePath& path, bool wait) {
    posix::UniqueFd fd{::open(path.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, path));
    struct flock request {};
    request.l_type = F_WRLCK;
    request.l_whence = SEEK_SET;
    request.l_start = 0;
    request.l_len = 0;
    while (::fcntl(fd.get(), wait ? F_OFD_SETLKW : F_OFD_SETLK, &request) != 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EACCES)
            return make_diag(ErrorDomain::Platform, kLockBusy).arg("path", path).kind(ErrorKind::Conflict).fail();
        return std::unexpected(posix::call_failed("fcntl", errno, path));
    }
    return ports::FileLock{std::make_unique<OfdLockHandle>(std::move(fd))};
}

Result<void> LinuxFileSystem::restrict_to_owner(const NativePath& path) { return posix_.restrict_to_owner(path); }

Result<ports::HeldFile> LinuxFileSystem::open_deny_write(const NativePath& path) {
    return posix_.open_deny_write(path);
}

Result<ports::FileRevision> LinuxFileSystem::revision(const NativePath& path) { return posix_.revision(path); }

Result<ports::SharedRead> LinuxFileSystem::read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) {
    return posix_.read_shared(path, offset, max_bytes);
}

Result<void> LinuxFileSystem::create_dirs_owner_only(const NativePath& path) {
    return posix_.create_dirs_owner_only(path);
}

Result<void> LinuxFileSystem::remove_tree(const NativePath& path) { return posix_.remove_tree(path); }

}  // namespace reboot::os_linux::platform
