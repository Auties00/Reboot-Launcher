#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/log_file_system.hpp"

namespace rb::os_macos::platform {

// ILogFileSystem over plain POSIX calls: files are created 0600 and flushed with fsync, since
// F_FULLFSYNC on every flush would stall the logger's writer.
class MacLogFileSystem final : public ports::ILogFileSystem {
public:
    Result<void> create_directories(const NativePath& dir) override;
    Result<ports::LogFile> open_append(const NativePath& path) override;
    Result<std::vector<ports::LogDirEntry>> list(const NativePath& dir) override;
    Result<void> remove(const NativePath& path) override;
};

}  // namespace rb::os_macos::platform
