#pragma once

#include <memory>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::os_windows::platform {

// Covers no capability ids; IFileWatcher over overlapped ReadDirectoryChangesW.
class WindowsFileWatcher final : public ports::IFileWatcher {
public:
    // Creates the completion port and the one thread every watch shares.
    [[nodiscard]] static Result<WindowsFileWatcher> create();

    ~WindowsFileWatcher() override;
    WindowsFileWatcher(WindowsFileWatcher&&) noexcept;
    WindowsFileWatcher& operator=(WindowsFileWatcher&&) noexcept;
    WindowsFileWatcher(const WindowsFileWatcher&) = delete;
    WindowsFileWatcher& operator=(const WindowsFileWatcher&) = delete;

    // A notify buffer overflow reports Modified on `dir` itself, meaning rescan.
    Result<ports::WatchHandle> watch(const NativePath& dir, UniqueFunction<void(ports::FileChange)> on_change) override;

private:
    struct Impl;

    explicit WindowsFileWatcher(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::os_windows::platform
