#include "inotify_events.hpp"

#include <sys/inotify.h>

namespace rb::os_linux::platform {

u32 inotify_watch_mask() noexcept {
    return IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF |
           IN_MOVE_SELF | IN_ONLYDIR;
}

std::optional<InotifyChange> map_inotify_event(u32 mask) noexcept {
    using ports::FileChangeKind;
    if ((mask & IN_Q_OVERFLOW) != 0) return InotifyChange{InotifySubject::AllDirectories, FileChangeKind::Modified};
    if ((mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT)) != 0)
        return InotifyChange{InotifySubject::Directory, FileChangeKind::Removed};
    if ((mask & IN_CREATE) != 0) return InotifyChange{InotifySubject::Entry, FileChangeKind::Created};
    if ((mask & IN_DELETE) != 0) return InotifyChange{InotifySubject::Entry, FileChangeKind::Removed};
    if ((mask & (IN_MOVED_FROM | IN_MOVED_TO)) != 0) return InotifyChange{InotifySubject::Entry, FileChangeKind::Renamed};
    if ((mask & (IN_CLOSE_WRITE | IN_MODIFY)) != 0) return InotifyChange{InotifySubject::Entry, FileChangeKind::Modified};
    return std::nullopt;
}

}  // namespace rb::os_linux::platform
