#include "reboot/os_macos/ipc/make_client_platform.hpp"

#include "unistd.hpp"

#include <memory>
#include <string>
#include <utility>

#include "reboot/os_macos/ipc/mac_caller_context.hpp"
#include "reboot/os_macos/ipc/mac_client_paths.hpp"
#include "reboot/os_macos/ipc/sm_app_service_engine_starter.hpp"
#include "reboot/os_macos/ipc/unix_socket_connector.hpp"
#include "reboot/posix/posix_file_system.hpp"

namespace rb::ports {

Result<ClientPlatform> make_client_platform() {
    using namespace os_macos::ipc;
    Result<MacClientPaths> paths = MacClientPaths::detect();
    if (!paths) return std::unexpected(std::move(paths.error()));
    Result<MacCallerContext> caller = MacCallerContext::detect();
    if (!caller) return std::unexpected(std::move(caller.error()));

    ClientPlatform platform;
    platform.connector = std::make_unique<UnixSocketConnector>(paths->ipc_runtime_base());
    platform.starter = std::make_unique<SmAppServiceEngineStarter>(static_cast<u32>(::geteuid()), caller->capture());
    platform.caller = std::make_unique<MacCallerContext>(std::move(*caller));
    platform.paths = std::make_unique<MacClientPaths>(std::move(*paths));
    platform.revisions = std::make_unique<posix::PosixFileRevisionReader>();
    platform.self = PeerIdentity{std::to_string(::geteuid()), static_cast<u32>(::getpid())};
    return platform;
}

}  // namespace rb::ports
