#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "reboot/posix/unix_socket_listener_base.hpp"

namespace reboot::os_linux::ipc {

// Covers no capability ids; IIpcListener for the engine over
// <linux_ipc_runtime_base(geteuid())>/reboot-launcher/<hash16>.sock, bound itself or inherited
// from systemd.
class UnixSocketListener final : public posix::UnixSocketListenerBase {
public:
    // Peers are checked with SO_PEERCRED against geteuid().
    explicit UnixSocketListener(NativePath runtime_base);

    // The path must be exactly <runtime_base>/reboot-launcher/<16 lowercase hex digits>.sock,
    // otherwise ipc.endpoint_untrusted caused by platform.ipc_endpoint_outside_runtime_dir.
    // runtime_base is created 0700 when missing (only the /tmp fallback can be) and lstat-checked:
    // a real directory owned by us with mode 0700. Then the posix base: the same check on
    // reboot-launcher/, the 108-byte sun_path limit, the lock and the 0600 bind. An inherited
    // socket is never unlinked, here or on close(), since systemd keeps listening on its path.
    Result<void> listen(std::string_view endpoint_name,
                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) override;

private:
    // Socket activation: only when LISTEN_PID is this process. LISTEN_FDS must be 1 and fd 3 a
    // listening AF_UNIX stream socket (platform.ipc_inherited_socket_invalid) bound to
    // `socket_path` (platform.ipc_inherited_socket_mismatch); it is made FD_CLOEXEC. The
    // LISTEN_* variables stay set, since children get an explicit envp. Not for us: nullopt, and
    // listen() binds its own.
    Result<std::optional<posix::UniqueFd>> take_inherited_socket(const NativePath& socket_path) override;

    NativePath runtime_base_;
};

}  // namespace reboot::os_linux::ipc
