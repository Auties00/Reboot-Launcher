#pragma once

#include <string>

#include "reboot/foundation/diag.hpp"
#include "win32.hpp"

namespace rb::os_windows::ipc {

// "S-1-5-..." for `sid`; ConvertSidToStringSidW failing is platform.ipc_call_failed.
[[nodiscard]] Result<std::string> sid_string(PSID sid);
// TokenUser of a token opened with TOKEN_QUERY.
[[nodiscard]] Result<std::string> token_user_sid(HANDLE token);

}  // namespace rb::os_windows::ipc
