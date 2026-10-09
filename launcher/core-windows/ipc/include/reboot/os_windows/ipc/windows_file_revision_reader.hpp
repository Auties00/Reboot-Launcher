#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::os_windows::ipc {

// Covers no capability ids; IFileRevisionReader for reboot_client, which may not link
// core-windows/platform. A failing Win32 call is platform.ipc_call_failed_on_path.
class WindowsFileRevisionReader final : public ports::IFileRevisionReader {
public:
    Result<ports::FileRevision> revision(const NativePath& path) override;
};

}  // namespace rb::os_windows::ipc
