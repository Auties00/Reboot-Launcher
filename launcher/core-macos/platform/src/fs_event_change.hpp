#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::os_macos::platform {

// The kFSEventStreamEventFlag bits of one event, reduced to what a watcher reports.
struct FsEventFlags {
    // MustScanSubDirs, UserDropped or KernelDropped: events were lost.
    bool dropped = false;
    bool created = false;
    bool removed = false;
    bool renamed = false;
    // Content, inode metadata, Finder info, owner or xattr changes.
    bool modified = false;
};

// Maps one event under `watched_real` (the realpath FSEvents reports against) onto `watched` (the
// caller's spelling). Only direct entries are reported; lost events report Modified on `watched`.
// FSEvents coalesces flags, so a removal only counts when the entry is gone (`exists` false).
[[nodiscard]] std::optional<ports::FileChange> fs_event_change(const NativePath& watched, const NativePath& watched_real,
                                                               std::string_view event_path, FsEventFlags flags,
                                                               bool exists);

}  // namespace rb::os_macos::platform
