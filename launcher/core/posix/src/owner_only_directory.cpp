#include "owner_only_directory.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>

#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::posix {

Result<bool> make_owner_only_directory(const NativePath& directory) {
    if (::mkdir(directory.c_str(), 0700) != 0) {
        if (errno == EEXIST) return false;
        return std::unexpected(call_failed("mkdir", errno, directory));
    }
    const UniqueFd fd{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
    if (!fd.valid()) return std::unexpected(call_failed("open", errno, directory));
    if (::fchmod(fd.get(), 0700) != 0) return std::unexpected(call_failed("fchmod", errno, directory));
    return true;
}

}  // namespace reboot::posix
