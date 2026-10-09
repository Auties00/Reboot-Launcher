#include "darwin.hpp"

#include "full_fsync.hpp"

#include <cerrno>
#include <fcntl.h>

#include "reboot/posix/posix_error.hpp"

namespace rb::os_macos::platform {

Result<void> full_fsync(int fd) {
    for (;;) {
        if (::fcntl(fd, F_FULLFSYNC) == 0) return {};
        if (errno == EINTR) continue;
        if (errno != ENOTSUP && errno != EINVAL && errno != ENOTTY) return std::unexpected(posix::call_failed("fcntl", errno));
        break;
    }
    while (::fsync(fd) != 0) {
        if (errno != EINTR) return std::unexpected(posix::call_failed("fsync", errno));
    }
    return {};
}

}  // namespace rb::os_macos::platform
