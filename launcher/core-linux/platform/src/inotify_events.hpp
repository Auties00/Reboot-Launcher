#pragma once

#include <optional>

#include "reboot/foundation/types.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::os_linux::platform {

// What one inotify event reports: an entry of the watched directory, the directory itself, or
// (on queue overflow) every watched directory, which must then be rescanned.
enum class InotifySubject : u8 { Entry, Directory, AllDirectories };

struct InotifyChange {
    InotifySubject subject{};
    ports::FileChangeKind kind{};
};

// The inotify mask every watch asks for.
[[nodiscard]] u32 inotify_watch_mask() noexcept;

// nullopt for events that report no change, such as IN_IGNORED.
[[nodiscard]] std::optional<InotifyChange> map_inotify_event(u32 mask) noexcept;

}  // namespace rb::os_linux::platform
