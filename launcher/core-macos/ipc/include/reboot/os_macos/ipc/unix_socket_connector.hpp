#pragma once

#include <chrono>
#include <memory>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/unix_socket_connector_base.hpp"

namespace rb::os_macos::ipc {

// Covers no capability ids; the reboot_client end of the engine socket behind IIpcConnector.
class UnixSocketConnector final : public posix::UnixSocketConnectorBase {
public:
    // The listener is checked with getpeereid against geteuid() before anything is written.
    explicit UnixSocketConnector(NativePath user_temp_dir);

    // Same path rule as UnixSocketListener::listen, then the posix base: the directory's owner
    // and 0700 mode, the 104-byte sun_path limit, connect within `deadline`, then the peer uid.
    Result<std::unique_ptr<ports::IByteStream>> connect(std::string_view endpoint_name,
                                                        std::chrono::milliseconds deadline) override;

private:
    NativePath user_temp_dir_;
};

}  // namespace rb::os_macos::ipc
