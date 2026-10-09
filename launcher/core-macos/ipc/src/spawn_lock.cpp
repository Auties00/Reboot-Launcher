#include "spawn_lock.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

#include <cerrno>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::ipc {

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

Result<posix::UniqueFd> lock_exclusive(const NativePath& path) {
    posix::UniqueFd fd{::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, path));
    while (::flock(fd.get(), LOCK_EX) != 0) {
        if (errno != EINTR) return std::unexpected(posix::call_failed("flock", errno, path));
    }
    return fd;
}

}  // namespace rb::os_macos::ipc
