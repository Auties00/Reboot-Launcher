#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::posix {

// Creates `path` with O_CREAT|O_EXCL|O_NOFOLLOW and mode 0600 whatever the umask, then writes
// `bytes`. Anything already there, a link included, fails with open's posix.call_failed_on_path.
[[nodiscard]] Result<void> write_exclusive_file(const NativePath& path, std::string_view bytes);

}  // namespace rb::posix
