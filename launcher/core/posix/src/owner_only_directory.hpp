#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::posix {

// mkdir 0700, then fchmod 0700 through an O_NOFOLLOW fd, since mkdir honours the umask. True when
// it created the directory, false when something already existed at the path.
[[nodiscard]] Result<bool> make_owner_only_directory(const NativePath& directory);

}  // namespace rb::posix
