#pragma once

#include <chrono>
#include <memory>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/unix_socket_connector_base.hpp"

namespace rb::os_linux::ipc {

// Covers no capability ids; the reboot_client end of the engine socket behind IIpcConnector.
class UnixSocketConnector final : public posix::UnixSocketConnectorBase {
public:
    // The listener is checked with SO_PEERCRED against geteuid() before anything is written.
    // With socket activation that pid is the user manager's; the engine's comes from Hello.
    explicit UnixSocketConnector(NativePath runtime_base);

    // Same path rule as UnixSocketListener::listen. runtime_base is lstat-checked but never
    // created: missing is posix.engine_not_listening (ErrorKind::EngineUnavailable), so the client
    // may autostart; a foreign owner or a mode other than 0700 is ipc.endpoint_untrusted. Then the
    // posix base: reboot-launcher/'s owner and mode, sun_path, connect within `deadline`, peer uid.
    Result<std::unique_ptr<ports::IByteStream>> connect(std::string_view endpoint_name,
                                                        std::chrono::milliseconds deadline) override;

private:
    NativePath runtime_base_;
};

}  // namespace rb::os_linux::ipc
