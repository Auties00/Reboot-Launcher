#pragma once

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/peer_credential_check.hpp"

namespace rb::posix {

// Covers no capability ids; the client side of the AF_UNIX engine endpoint behind IIpcConnector.
// `endpoint_name` is the socket path. The returned stream runs on a thread it owns.
// The host app keeps SIGPIPE fatal, so writes use MSG_NOSIGNAL (Linux) or SO_NOSIGPIPE (macOS),
// and the socket is close-on-exec so the app's children never hold the engine connection.
class UnixSocketConnectorBase : public ports::IIpcConnector {
public:
    // Checks the directory (lstat: a real directory, our uid, mode 0700) and sun_path, connects
    // within `deadline`, then verifies the listener's uid before returning, so nothing is written
    // to a foreign endpoint. Untrusted: ipc.endpoint_untrusted. A missing directory or socket, a
    // refused connect and one that does not complete within `deadline` are all
    // posix.engine_not_listening with ErrorKind::EngineUnavailable: the client may autostart and
    // keeps polling until its own deadline ends in ipc.engine_unavailable.
    Result<std::unique_ptr<ports::IByteStream>> connect(std::string_view endpoint_name,
                                                        std::chrono::milliseconds deadline) override;

protected:
    explicit UnixSocketConnectorBase(PeerCredentialCheck peer_check) noexcept : peer_check_(std::move(peer_check)) {}

private:
    PeerCredentialCheck peer_check_;
};

}  // namespace rb::posix
