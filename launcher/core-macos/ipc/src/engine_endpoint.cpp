#include "unistd.hpp"

#include <memory>
#include <string>
#include <utility>

#include "darwin_user_temp_dir.hpp"
#include "reboot/os_macos/ipc/unix_socket_listener.hpp"
#include "reboot/ports/platform_services.hpp"
#include "reboot/posix/ignore_sigpipe.hpp"

namespace rb::ports {

Result<EngineEndpoint> make_engine_endpoint() {
    // Child stdio pipes and sockets whose peer left must fail with EPIPE, not end the engine.
    if (Result<void> ignored = posix::ignore_sigpipe(); !ignored) return std::unexpected(std::move(ignored.error()));
    Result<NativePath> temp = os_macos::ipc::darwin_user_temp_dir();
    if (!temp) return std::unexpected(std::move(temp.error()));
    EngineEndpoint endpoint;
    endpoint.self = PeerIdentity{std::to_string(::geteuid()), static_cast<u32>(::getpid())};
    endpoint.listener = std::make_unique<os_macos::ipc::UnixSocketListener>(std::move(*temp));
    return endpoint;
}

}  // namespace rb::ports
