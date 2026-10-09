#include "reboot/posix/unique_fd.hpp"

#include "unistd.hpp"

namespace rb::posix {

UniqueFd::~UniqueFd() { reset(); }

void UniqueFd::reset(int fd) noexcept {
    // POSIX leaves the fd state unspecified after EINTR; retrying could close a reused fd.
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

}  // namespace rb::posix
