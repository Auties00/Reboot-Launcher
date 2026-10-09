#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"
#include "reboot/os_linux/ipc/unix_socket_listener.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/posix/ignore_sigpipe.hpp"

namespace rb::ports {

Result<EngineEndpoint> make_engine_endpoint() {
    // Child stdio pipes and sockets whose peer left must fail with EPIPE, not end the engine.
    if (Result<void> ignored = posix::ignore_sigpipe(); !ignored) return std::unexpected(std::move(ignored.error()));
    const u32 uid = static_cast<u32>(::geteuid());
    EngineEndpoint endpoint;
    endpoint.self = PeerIdentity{std::to_string(uid), static_cast<u32>(::getpid())};
    endpoint.listener = std::make_unique<os_linux::ipc::UnixSocketListener>(os_linux::ipc::linux_ipc_runtime_base(uid).path);
    return endpoint;
}

}  // namespace rb::ports
