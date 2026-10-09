#include "state_locks.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_linux::ipc {
namespace {

[[nodiscard]] struct flock whole_file_write_lock() noexcept {
    struct flock lock {};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    return lock;
}

}  // namespace

Result<void> create_private_dirs(const NativePath& directory) {
    NativePath current;
    for (const NativePath& part : directory) {
        current /= part;
        if (current == current.root_path()) continue;
        if (::mkdir(current.c_str(), 0700) == 0) {
            // mkdir honours the umask.
            if (::chmod(current.c_str(), 0700) != 0)
                return std::unexpected(posix::call_failed("chmod", errno, current));
        } else if (errno != EEXIST) {
            return std::unexpected(posix::call_failed("mkdir", errno, current));
        }
    }
    return {};
}

Result<posix::UniqueFd> lock_ofd_exclusive(const NativePath& path) {
    posix::UniqueFd fd{::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, path));
    struct flock lock = whole_file_write_lock();
    while (::fcntl(fd.get(), F_OFD_SETLKW, &lock) != 0) {
        if (errno != EINTR) return std::unexpected(posix::call_failed("fcntl", errno, path));
    }
    return fd;
}

Result<bool> is_ofd_locked(const NativePath& path) {
    const posix::UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (!fd.valid()) {
        if (errno == ENOENT) return false;
        return std::unexpected(posix::call_failed("open", errno, path));
    }
    struct flock lock = whole_file_write_lock();
    if (::fcntl(fd.get(), F_OFD_GETLK, &lock) != 0) return std::unexpected(posix::call_failed("fcntl", errno, path));
    return lock.l_type != F_UNLCK;
}

}  // namespace rb::os_linux::ipc
