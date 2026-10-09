#include "reboot/posix/posix_error.hpp"

#include <cerrno>

#include "messages.hpp"

namespace rb::posix {

namespace {

[[nodiscard]] ErrorKind kind_of(int error) noexcept {
    return error == ENOENT ? ErrorKind::NotFound : ErrorKind::Generic;
}

}  // namespace

Diagnostic call_failed(std::string_view call, int error) {
    return make_diag(ErrorDomain::Posix, kCallFailed).arg("call", call).os(errno_error(error)).kind(kind_of(error));
}

Diagnostic call_failed(std::string_view call, int error, const NativePath& path) {
    return make_diag(ErrorDomain::Posix, kCallFailedOnPath)
        .arg("call", call)
        .arg("path", path)
        .os(errno_error(error))
        .kind(kind_of(error));
}

}  // namespace rb::posix
