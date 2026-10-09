#pragma once

#include <memory>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/unix_socket_listener_base.hpp"

namespace rb::os_macos::ipc {

// Covers no capability ids; IIpcListener for the engine over
// <user_temp_dir>/reboot-launcher/<hash16>.sock, where `user_temp_dir` is
// confstr(_CS_DARWIN_USER_TEMP_DIR).
class UnixSocketListener final : public posix::UnixSocketListenerBase {
public:
    // Peers are checked with getpeereid (LOCAL_PEERPID for the pid) against geteuid().
    explicit UnixSocketListener(NativePath user_temp_dir);

    // The path must be exactly <user_temp_dir>/reboot-launcher/<16 lowercase hex digits>.sock,
    // otherwise ipc.endpoint_untrusted caused by platform.endpoint_outside_user_temp; an empty
    // name (endpoint_name when confstr failed) fails the same way. Then the posix base: the
    // directory owned by us with mode 0700, the 104-byte sun_path limit, the lock and the 0600 bind.
    Result<void> listen(std::string_view endpoint_name,
                        UniqueFunction<void(std::unique_ptr<ports::IByteStream>)> on_accept) override;

private:
    NativePath user_temp_dir_;
};

}  // namespace rb::os_macos::ipc
