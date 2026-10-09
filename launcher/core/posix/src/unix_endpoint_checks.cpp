#include "unix_endpoint_checks.hpp"

#include <cerrno>
#include <sys/stat.h>
#include <sys/un.h>

#include "messages.hpp"
#include "owner_only_directory.hpp"
#include "reboot/posix/posix_error.hpp"

namespace rb::posix {

std::size_t sun_path_capacity() noexcept { return sizeof(sockaddr_un::sun_path); }

Result<void> check_socket_path_fits(const NativePath& socket, std::size_t capacity) {
    const std::size_t length = socket.native().size() + 1;
    if (length <= capacity) return {};
    return make_diag(ErrorDomain::Posix, kSocketPathTooLong)
        .arg("path", socket)
        .arg("length", length)
        .arg("limit", capacity)
        .kind(ErrorKind::InvalidInput)
        .fail();
}

std::string octal_mode(u32 mode) {
    std::string out(4, '0');
    for (std::size_t i = 4; i-- > 0; mode >>= 3) out[i] = static_cast<char>('0' + (mode & 7U));
    return out;
}

Result<void> check_private_directory(const NativePath& directory, u32 uid) {
    struct stat info {};
    if (::lstat(directory.c_str(), &info) != 0) return std::unexpected(call_failed("lstat", errno, directory));
    if (!S_ISDIR(info.st_mode)) {
        return make_diag(ErrorDomain::Ipc, kEndpointUntrusted)
            .cause(make_diag(ErrorDomain::Posix, kNotADirectory).arg("path", directory))
            .fail();
    }
    const u32 mode = static_cast<u32>(info.st_mode) & 07777U;
    if (info.st_uid != uid || mode != 0700U) {
        return make_diag(ErrorDomain::Ipc, kEndpointUntrusted)
            .cause(make_diag(ErrorDomain::Posix, kDirectoryNotPrivate)
                       .arg("path", directory)
                       .arg("expected_uid", uid)
                       .arg("owner_uid", static_cast<u32>(info.st_uid))
                       .arg("mode", octal_mode(mode)))
            .fail();
    }
    return {};
}

Result<void> ensure_private_directory(const NativePath& directory, u32 uid) {
    if (auto made = make_owner_only_directory(directory); !made) return std::unexpected(std::move(made.error()));
    return check_private_directory(directory, uid);
}

}  // namespace rb::posix
