#pragma once

#include <string>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/integration/desktop_check.hpp"

namespace reboot {
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::contracts::ipc {
struct CallerContext;
}

namespace reboot::ports {
class IShellLauncher;
class ISystemInfo;
}  // namespace reboot::ports

namespace reboot::integration {

// Covers os-integration.desktop-services, its open-in-shell part; the rest stays in each UI.
// Strand-only: validates and checks the caller's desktop here, calls the shell on the WorkerPool.
class ShellService {
public:
    using Done = UniqueFunction<void(Result<void>)>;

    ShellService(ports::IShellLauncher& shell, const ports::ISystemInfo& system, DesktopCheck desktop_check,
                 WorkerPool& workers, Executor& strand);
    ShellService(const ShellService&) = delete;
    ShellService& operator=(const ShellService&) = delete;

    // integration.url_not_https unless is_openable_url.
    void open_url(const contracts::ipc::CallerContext& caller, std::string url, CancelToken token, Done done);
    // integration.path_not_absolute for a relative path.
    void open_path(const contracts::ipc::CallerContext& caller, NativePath path, CancelToken token, Done done);
    void reveal(const contracts::ipc::CallerContext& caller, NativePath path, CancelToken token, Done done);

private:
    ports::IShellLauncher& shell_;
    const ports::ISystemInfo& system_;
    DesktopCheck desktop_check_;
    WorkerPool& workers_;
    Executor& strand_;
};

}  // namespace reboot::integration
