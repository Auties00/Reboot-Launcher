#include "reboot/os_linux/ipc/make_client_platform.hpp"

#include <memory>
#include <string>
#include <unistd.h>
#include <utility>

#include "reboot/os_linux/ipc/linux_caller_context.hpp"
#include "reboot/os_linux/ipc/linux_client_paths.hpp"
#include "reboot/os_linux/ipc/systemd_engine_starter.hpp"
#include "reboot/os_linux/ipc/unix_socket_connector.hpp"
#include "reboot/posix/posix_file_system.hpp"
#include "steam_reaper.hpp"

namespace rb::ports {

Result<ClientPlatform> make_client_platform() {
    using os_linux::ipc::LinuxCallerContext;
    using os_linux::ipc::LinuxClientPaths;
    Result<LinuxClientPaths> paths = LinuxClientPaths::detect();
    if (!paths) return std::unexpected(std::move(paths.error()));
    LinuxCallerContext caller = LinuxCallerContext::detect();
    const bool under_steam_reaper = os_linux::ipc::has_steam_reaper_ancestor();

    ClientPlatform platform;
    platform.connector = std::make_unique<os_linux::ipc::UnixSocketConnector>(paths->runtime_base().path);
    platform.starter =
        std::make_unique<os_linux::ipc::SystemdEngineStarter>(caller.capture(), *paths, under_steam_reaper);
    platform.caller = std::make_unique<LinuxCallerContext>(std::move(caller));
    platform.paths = std::make_unique<LinuxClientPaths>(std::move(*paths));
    platform.revisions = std::make_unique<posix::PosixFileRevisionReader>();
    platform.self = PeerIdentity{std::to_string(::geteuid()), static_cast<u32>(::getpid())};
    return platform;
}

}  // namespace rb::ports
