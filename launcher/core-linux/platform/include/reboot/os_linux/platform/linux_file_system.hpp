#pragma once

#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"
#include "reboot/posix/posix_file_system.hpp"

namespace reboot::os_linux::platform {

// Covers no capability ids; IFileSystem as posix::PosixFileSystem with open-file-description
// locks, fsync of the written file, and fsync of its directory after the rename, without which
// ext4 and btrfs may lose the rename on power loss.
class LinuxFileSystem final : public ports::IFileSystem {
public:
    LinuxFileSystem() noexcept;

    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override;
    Result<std::vector<u8>> read_all(const NativePath& path) override;
    // F_OFD_SETLK, or F_OFD_SETLKW with `wait`: owned by the open file, so a lock taken on one
    // thread is not dropped when another thread closes an unrelated fd of the same file, as a
    // classic POSIX record lock would be.
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override;
    Result<void> restrict_to_owner(const NativePath& path) override;
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override;
    Result<ports::FileRevision> revision(const NativePath& path) override;
    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override;
    Result<void> create_dirs_owner_only(const NativePath& path) override;
    Result<void> remove_tree(const NativePath& path) override;

private:
    posix::PosixFileSystem posix_;
};

}  // namespace reboot::os_linux::platform
