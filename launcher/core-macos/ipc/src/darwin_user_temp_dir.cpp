#include "darwin_user_temp_dir.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <string>
#include <utility>

#include "messages.hpp"

namespace reboot::os_macos::ipc {
namespace {

[[nodiscard]] Diagnostic unavailable(int error) {
    DiagBuilder diag = make_diag(ErrorDomain::Platform, kUserTempDirUnavailable);
    // confstr leaves errno alone when the variable merely has no value.
    if (error != 0) return std::move(diag).os(SystemError{SystemError::Origin::Host, error});
    return diag;
}

}  // namespace

Result<NativePath> darwin_user_temp_dir() {
    errno = 0;
    const std::size_t size = ::confstr(_CS_DARWIN_USER_TEMP_DIR, nullptr, 0);
    if (size == 0) return std::unexpected(unavailable(errno));
    std::string buffer(size, '\0');
    const std::size_t written = ::confstr(_CS_DARWIN_USER_TEMP_DIR, buffer.data(), buffer.size());
    if (written == 0 || written > size) return std::unexpected(unavailable(errno));
    buffer.resize(written - 1);
    NativePath directory{std::move(buffer)};
    if (!directory.is_absolute()) return std::unexpected(unavailable(0));
    return directory;
}

}  // namespace reboot::os_macos::ipc
