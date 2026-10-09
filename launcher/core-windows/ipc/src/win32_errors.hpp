#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "win32.hpp"

namespace reboot::os_windows::ipc {

// platform.ipc_call_failed carrying `error` (a Win32 code or an HRESULT).
[[nodiscard]] Diagnostic call_failed(std::string_view call, DWORD error);
[[nodiscard]] Diagnostic call_failed(std::string_view call);
[[nodiscard]] Diagnostic call_failed_on_path(std::string_view call, const NativePath& path, DWORD error);
// platform.pipe_call_failed for a call on the engine pipe `name`.
[[nodiscard]] Diagnostic pipe_call_failed(std::string_view call, std::string_view name, DWORD error);
// ipc.endpoint_untrusted caused by `cause`.
[[nodiscard]] Diagnostic untrusted(Diagnostic cause);

}  // namespace reboot::os_windows::ipc
