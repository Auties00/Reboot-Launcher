#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::os_linux::platform {

// Covers no capability ids; IFileWatcher over one inotify instance.
class InotifyWatcher final : public ports::IFileWatcher {
public:
    // Creates the inotify fd (IN_CLOEXEC) and the thread that reads it.
    InotifyWatcher();
    ~InotifyWatcher() override;
    InotifyWatcher(const InotifyWatcher&) = delete;
    InotifyWatcher& operator=(const InotifyWatcher&) = delete;

    // Direct entries of `dir` only (inotify is not recursive). IN_CREATE gives Created,
    // IN_DELETE Removed, IN_CLOSE_WRITE and IN_MODIFY Modified, IN_MOVED_FROM/IN_MOVED_TO
    // Renamed. IN_Q_OVERFLOW reports Modified on `dir` itself, meaning rescan; IN_DELETE_SELF and
    // IN_MOVE_SELF report Removed on `dir`. Watching the same directory twice shares one kernel
    // watch. Hitting fs.inotify.max_user_watches fails with platform.watch_limit.
    Result<ports::WatchHandle> watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::os_linux::platform
