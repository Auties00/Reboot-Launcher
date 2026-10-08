#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::posix {

[[nodiscard]] inline SystemError errno_error(int code) noexcept { return SystemError{SystemError::Origin::Host, code}; }

// posix.call_failed with `error` as the OS error; ENOENT maps to ErrorKind::NotFound.
[[nodiscard]] Diagnostic call_failed(std::string_view call, int error);
[[nodiscard]] Diagnostic call_failed(std::string_view call, int error, const NativePath& path);

}  // namespace reboot::posix
