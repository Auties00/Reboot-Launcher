#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/log_file_system.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; ILogFileSystem over Win32 file APIs with \\?\ paths. Log files are
// opened FILE_SHARE_READ | WRITE | DELETE, so export and pruning work while the logger writes.
class WindowsLogFileSystem final : public ports::ILogFileSystem {
public:
    Result<void> create_directories(const NativePath& dir) override;
    // A final reparse point is refused rather than followed.
    Result<ports::LogFile> open_append(const NativePath& path) override;
    Result<std::vector<ports::LogDirEntry>> list(const NativePath& dir) override;
    // The name goes at once, even while the logger still holds the file open.
    Result<void> remove(const NativePath& path) override;
};

}  // namespace rb::os_windows::platform
