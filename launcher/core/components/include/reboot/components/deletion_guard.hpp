#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot {
class Executor;
}

namespace reboot::ports {
class IFileWatcher;
}

namespace reboot::components {

// Capabilities: dll-injection.deletion-guard.
// Strand-only. Watches the directory of every tracked file through IFileWatcher and reports a
// tracked file that is removed, renamed or modified. A report is only a suspect: an AV scan that
// adds a stream or touches attributes also raises Modified, so the owner re-verifies the file
// before raising anything.
// - Each directory watch has an id; watcher events are posted to the strand with it and dropped
//   once that watch has ended.
// - Like every strand service, it is destroyed only after the strand stopped running tasks.
class DeletionGuard {
public:
    using OnChanged = UniqueFunction<void(const NativePath& file)>;

    DeletionGuard(ports::IFileWatcher& watcher, Executor& strand, OnChanged on_changed);
    ~DeletionGuard();
    DeletionGuard(const DeletionGuard&) = delete;
    DeletionGuard& operator=(const DeletionGuard&) = delete;

    // Watches the file's directory on first use.
    Result<void> track(const NativePath& file);
    // Called before the store deletes the file itself; ends a directory watch with no files left.
    void untrack(const NativePath& file);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::components
