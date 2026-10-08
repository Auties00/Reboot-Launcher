#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::os_macos::platform {

// Covers no capability ids; IFileWatcher over FSEvents, reporting direct entries of `dir` only.
class FsEventsWatcher final : public ports::IFileWatcher {
public:
    FsEventsWatcher();
    ~FsEventsWatcher() override;
    FsEventsWatcher(const FsEventsWatcher&) = delete;
    FsEventsWatcher& operator=(const FsEventsWatcher&) = delete;

    // Dropped events report Modified on `dir` itself, which asks the caller to rescan.
    Result<ports::WatchHandle> watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::os_macos::platform
