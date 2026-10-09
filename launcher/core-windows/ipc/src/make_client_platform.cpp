#include "reboot/os_windows/ipc/make_client_platform.hpp"

#include <memory>
#include <utility>

#include "reboot/os_windows/ipc/named_pipe_connector.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/os_windows/ipc/windows_caller_context.hpp"
#include "reboot/os_windows/ipc/windows_client_paths.hpp"
#include "reboot/os_windows/ipc/windows_engine_starter.hpp"
#include "reboot/os_windows/ipc/windows_file_revision_reader.hpp"

namespace reboot::ports {

Result<ClientPlatform> make_client_platform() {
    using namespace os_windows::ipc;
    Result<PipeTrust> trust = PipeTrust::for_current_process();
    if (!trust) return std::unexpected(std::move(trust.error()));
    Result<WindowsCallerContext> caller = WindowsCallerContext::detect();
    if (!caller) return std::unexpected(std::move(caller.error()));
    Result<WindowsClientPaths> paths = WindowsClientPaths::detect();
    if (!paths) return std::unexpected(std::move(paths.error()));

    ClientPlatform platform;
    platform.self = trust->self();
    platform.revisions = std::make_unique<WindowsFileRevisionReader>();
    platform.starter = std::make_unique<WindowsEngineStarter>(trust->user_sid(), caller->capture());
    platform.connector = std::make_unique<NamedPipeConnector>(std::move(*trust));
    platform.caller = std::make_unique<WindowsCallerContext>(std::move(*caller));
    platform.paths = std::make_unique<WindowsClientPaths>(std::move(*paths));
    return platform;
}

}  // namespace reboot::ports
