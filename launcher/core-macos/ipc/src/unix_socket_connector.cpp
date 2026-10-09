#include "reboot/os_macos/ipc/unix_socket_connector.hpp"

#include "unistd.hpp"

#include <utility>

#include "darwin_peer_credentials.hpp"
#include "engine_socket_path.hpp"

namespace rb::os_macos::ipc {

UnixSocketConnector::UnixSocketConnector(NativePath user_temp_dir)
    : UnixSocketConnectorBase(posix::PeerCredentialCheck{[](int fd) { return read_darwin_peer(fd); },
                                                         static_cast<u32>(::geteuid())}),
      user_temp_dir_(std::move(user_temp_dir)) {}

Result<std::unique_ptr<ports::IByteStream>> UnixSocketConnector::connect(std::string_view endpoint_name,
                                                                         std::chrono::milliseconds deadline) {
    if (auto socket = check_engine_socket_path(user_temp_dir_, endpoint_name); !socket)
        return std::unexpected(std::move(socket.error()));
    return UnixSocketConnectorBase::connect(endpoint_name, deadline);
}

}  // namespace rb::os_macos::ipc
