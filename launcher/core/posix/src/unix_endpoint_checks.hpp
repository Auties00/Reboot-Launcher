#pragma once

#include <cstddef>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::posix {

// sizeof(sockaddr_un::sun_path): 104 on macOS, 108 on Linux.
[[nodiscard]] std::size_t sun_path_capacity() noexcept;

// posix.socket_path_too_long when the path and its terminator do not fit `capacity` bytes;
// callers pass sun_path_capacity().
[[nodiscard]] Result<void> check_socket_path_fits(const NativePath& socket, std::size_t capacity);

// The permission bits of `mode` as four octal digits ("0700"), the form posix.directory_not_private takes.
[[nodiscard]] std::string octal_mode(u32 mode);

// lstat: a real directory (not a link), owned by `uid`, mode exactly 0700. Fails with
// ipc.endpoint_untrusted caused by posix.not_a_directory or posix.directory_not_private, or with
// lstat's posix.call_failed_on_path, ErrorKind::NotFound when the directory is missing.
[[nodiscard]] Result<void> check_private_directory(const NativePath& directory, u32 uid);

// Creates only the last component when it is missing (make_owner_only_directory), then
// check_private_directory.
[[nodiscard]] Result<void> ensure_private_directory(const NativePath& directory, u32 uid);

}  // namespace reboot::posix
