#pragma once

#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_windows::platform {

// Covers no capability ids; IFileSystem over Win32 file APIs, with \\?\ paths so MAX_PATH never applies.
class WindowsFileSystem final : public ports::IFileSystem {
public:
    // Retries sharing violations briefly: AV scanners and the indexer hold fresh files open.
    Result<void> atomic_replace(const NativePath& target, std::span<const u8> bytes, bool keep_backup) override;
    Result<std::vector<u8>> read_all(const NativePath& path) override;
    Result<ports::FileLock> lock_exclusive(const NativePath& path, bool wait) override;
    Result<void> restrict_to_owner(const NativePath& path) override;
    // FILE_SHARE_READ only, so nothing can write, rename or delete the file while it is held.
    Result<ports::HeldFile> open_deny_write(const NativePath& path) override;
    Result<ports::FileRevision> revision(const NativePath& path) override;
    // Shares read, write and delete, so the game keeps writing and renaming its log meanwhile.
    Result<ports::SharedRead> read_shared(const NativePath& path, u64 offset, std::size_t max_bytes) override;
    // Existing components keep their DACL.
    Result<void> create_dirs_owner_only(const NativePath& path) override;
    // Never enters a junction or symlink, and clears the read-only attribute installed payloads carry.
    Result<void> remove_tree(const NativePath& path) override;
};

}  // namespace reboot::os_windows::platform
