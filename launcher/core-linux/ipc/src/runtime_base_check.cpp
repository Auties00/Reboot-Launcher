#include "runtime_base_check.hpp"

#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>

#include "messages.hpp"
#include "reboot/posix/posix_error.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::os_linux::ipc {
namespace {

[[nodiscard]] std::string octal_mode(u32 mode) {
    std::string out(4, '0');
    for (std::size_t i = 4; i-- > 0; mode >>= 3) out[i] = static_cast<char>('0' + (mode & 7U));
    return out;
}

}  // namespace

Result<void> check_runtime_base(const NativePath& runtime_base, u32 uid) {
    struct stat info {};
    if (::lstat(runtime_base.c_str(), &info) != 0)
        return std::unexpected(posix::call_failed("lstat", errno, runtime_base));
    if (!S_ISDIR(info.st_mode)) {
        return make_diag(ErrorDomain::Ipc, posix::kEndpointUntrusted)
            .cause(make_diag(ErrorDomain::Posix, posix::kNotADirectory).arg("path", runtime_base))
            .fail();
    }
    const u32 mode = static_cast<u32>(info.st_mode) & 07777U;
    if (info.st_uid != uid || mode != 0700U) {
        return make_diag(ErrorDomain::Ipc, posix::kEndpointUntrusted)
            .cause(make_diag(ErrorDomain::Posix, posix::kDirectoryNotPrivate)
                       .arg("path", runtime_base)
                       .arg("expected_uid", uid)
                       .arg("owner_uid", static_cast<u32>(info.st_uid))
                       .arg("mode", octal_mode(mode)))
            .fail();
    }
    return {};
}

Result<void> ensure_runtime_base(const NativePath& runtime_base, u32 uid) {
    if (::mkdir(runtime_base.c_str(), 0700) == 0) {
        // mkdir honours the umask; the fd refuses a link swapped in since.
        const posix::UniqueFd fd{::open(runtime_base.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)};
        if (!fd.valid()) return std::unexpected(posix::call_failed("open", errno, runtime_base));
        if (::fchmod(fd.get(), 0700) != 0) return std::unexpected(posix::call_failed("fchmod", errno, runtime_base));
    } else if (errno != EEXIST) {
        return std::unexpected(posix::call_failed("mkdir", errno, runtime_base));
    }
    return check_runtime_base(runtime_base, uid);
}

}  // namespace reboot::os_linux::ipc
