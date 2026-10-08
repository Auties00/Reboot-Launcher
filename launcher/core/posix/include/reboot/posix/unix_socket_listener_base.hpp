#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/peer_credential_check.hpp"
#include "reboot/posix/unique_fd.hpp"

namespace reboot::posix {

// Covers no capability ids; the AF_UNIX engine endpoint behind IIpcListener on macOS and Linux.
// `endpoint_name` is the socket path. Accepting runs on a thread the listener owns, and a peer of
// another uid is closed before any byte is read. Each accepted stream runs on a thread of its
// own, so it may outlive the listener.
class UnixSocketListenerBase : public ports::IIpcListener {
public:
    ~UnixSocketListenerBase() override;
    UnixSocketListenerBase(const UnixSocketListenerBase&) = delete;
    UnixSocketListenerBase& operator=(const UnixSocketListenerBase&) = delete;

    // Creates the socket's directory 0700 and lstat-checks its owner and mode, checks the path
    // against sun_path, unlinks a stale socket, then binds the socket 0600. The caller holds
    // state/engine.lock (EngineLock), so no other engine serves this path. The socket and every
    // accepted fd are close-on-exec, so no child inherits the endpoint.
    Result<void> listen(std::string_view endpoint_name,
                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) override;
    // Stops accepting and unlinks a socket this listener bound; idempotent.
    void close() override;

protected:
    explicit UnixSocketListenerBase(PeerCredentialCheck peer_check);

    // A socket the service manager already bound at `socket_path` (Linux LISTEN_FDS); nullopt
    // makes listen() bind its own. The directory checks apply either way, so reboot-engine.socket
    // must set DirectoryMode=0700 and SocketMode=0600 (systemd defaults to 0755 and 0666).
    virtual Result<std::optional<UniqueFd>> take_inherited_socket(const NativePath& socket_path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::posix
