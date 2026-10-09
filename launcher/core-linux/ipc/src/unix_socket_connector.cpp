#include "reboot/os_linux/ipc/unix_socket_connector.hpp"

#include <unistd.h>
#include <utility>

#include "engine_socket_path.hpp"
#include "linux_peer_credentials.hpp"
#include "messages.hpp"
#include "runtime_base_check.hpp"

namespace rb::os_linux::ipc {

UnixSocketConnector::UnixSocketConnector(NativePath runtime_base)
    : UnixSocketConnectorBase(posix::PeerCredentialCheck{[](int fd) { return read_linux_peer(fd); },
                                                         static_cast<u32>(::geteuid())}),
      runtime_base_(std::move(runtime_base)) {}

Result<std::unique_ptr<ports::IByteStream>> UnixSocketConnector::connect(std::string_view endpoint_name,
                                                                         std::chrono::milliseconds deadline) {
    auto socket = check_engine_socket_path(runtime_base_, endpoint_name);
    if (!socket) return std::unexpected(std::move(socket.error()));
    if (auto base = check_runtime_base(runtime_base_, static_cast<u32>(::geteuid())); !base) {
        // Only the engine creates the /tmp fallback, so a missing one means none has listened yet.
        if (base.error().kind == ErrorKind::NotFound) {
            return make_diag(ErrorDomain::Posix, posix::kEngineNotListening)
                .arg("path", *socket)
                .kind(ErrorKind::EngineUnavailable)
                .retryable()
                .fail();
        }
        return std::unexpected(std::move(base.error()));
    }
    return UnixSocketConnectorBase::connect(endpoint_name, deadline);
}

}  // namespace rb::os_linux::ipc
