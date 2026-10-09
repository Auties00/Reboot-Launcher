#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_windows::platform {

// A missing file, directory, drive or share is NotFound; a sharing or lock violation is Conflict.
[[nodiscard]] ErrorKind kind_of_win32(u32 error) noexcept;
// Win32 errors wrapped as HRESULT_FROM_WIN32 map like the bare code.
[[nodiscard]] ErrorKind kind_of_hresult(i32 hr) noexcept;

// platform.call_failed with the Win32 error as the os_error.
[[nodiscard]] Diagnostic call_failed(std::string_view call, u32 error);
[[nodiscard]] Diagnostic call_failed(std::string_view call, u32 error, const NativePath& path);
// The same for a COM call, with the HRESULT as the os_error.
[[nodiscard]] Diagnostic hresult_failed(std::string_view call, i32 hr);
[[nodiscard]] Diagnostic hresult_failed(std::string_view call, i32 hr, const NativePath& path);

}  // namespace reboot::os_windows::platform
