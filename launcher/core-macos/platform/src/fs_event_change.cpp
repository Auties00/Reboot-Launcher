#include "fs_event_change.hpp"

namespace reboot::os_macos::platform {

std::optional<ports::FileChange> fs_event_change(const NativePath& watched, const NativePath& watched_real,
                                                 std::string_view event_path, FsEventFlags flags, bool exists) {
    if (flags.dropped) return ports::FileChange{watched, ports::FileChangeKind::Modified};
    NativePath path{std::string(event_path)};
    if (!path.has_filename()) path = path.parent_path();
    if (path.parent_path() != watched_real) return std::nullopt;
    ports::FileChangeKind kind = ports::FileChangeKind::Modified;
    if (flags.renamed) {
        kind = ports::FileChangeKind::Renamed;
    } else if (flags.removed && !exists) {
        kind = ports::FileChangeKind::Removed;
    } else if (flags.created) {
        kind = ports::FileChangeKind::Created;
    } else if (!flags.modified && !flags.removed) {
        return std::nullopt;
    }
    return ports::FileChange{watched / path.filename(), kind};
}

}  // namespace reboot::os_macos::platform
