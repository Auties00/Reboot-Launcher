#include "reboot/os_macos/ipc/unix_socket_listener.hpp"

#include "unistd.hpp"

#include <utility>

#include "darwin_peer_credentials.hpp"
#include "engine_socket_path.hpp"

namespace reboot::os_macos::ipc {

UnixSocketListener::UnixSocketListener(NativePath user_temp_dir)
    : UnixSocketListenerBase(posix::PeerCredentialCheck{[](int fd) { return read_darwin_peer(fd); },
                                                        static_cast<u32>(::geteuid())}),
      user_temp_dir_(std::move(user_temp_dir)) {}

Result<void> UnixSocketListener::listen(std::string_view endpoint_name,
                                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) {
    if (auto socket = check_engine_socket_path(user_temp_dir_, endpoint_name); !socket)
        return std::unexpected(std::move(socket.error()));
    return UnixSocketListenerBase::listen(endpoint_name, std::move(on_accept));
}

}  // namespace reboot::os_macos::ipc
