#pragma once

#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/posix/posix_file_system.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; PosixFileSystem plus F_FULLFSYNC, since APFS fsync skips the drive cache.
class MacFileSystem final : public ports::IFileSystem {
public:
    MacFileSystem() noexcept;

    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override;
    Result<std::vector<u8>> read_all(const NativePath& path) override;
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override;
    Result<void> restrict_to_owner(const NativePath& path) override;
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override;
    Result<ports::FileRevision> revision(const NativePath& path) override;
    Result<void> create_dirs_owner_only(const NativePath& path) override;
    Result<void> remove_tree(const NativePath& path) override;

private:
    posix::PosixFileSystem posix_;
};

}  // namespace reboot::os_macos::platform
