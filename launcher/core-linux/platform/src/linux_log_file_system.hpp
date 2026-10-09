#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/log_file_system.hpp"

namespace reboot::os_linux::platform {

// ILogFileSystem over plain POSIX calls: files are created 0600 and synced with fdatasync.
class LinuxLogFileSystem final : public ports::ILogFileSystem {
public:
    Result<void> create_directories(const NativePath& dir) override;
    Result<ports::LogFile> open_append(const NativePath& path) override;
    Result<std::vector<ports::LogDirEntry>> list(const NativePath& dir) override;
    Result<void> remove(const NativePath& path) override;
};

}  // namespace reboot::os_linux::platform
