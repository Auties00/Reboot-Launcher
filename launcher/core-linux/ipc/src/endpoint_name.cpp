#include <optional>
#include <string>
#include <string_view>

#include "decimal_uid.hpp"
#include "engine_socket_path.hpp"
#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::ports {

// Empty when self.user_id is not a decimal uid, which UnixSocketListener and UnixSocketConnector
// reject. ipc::endpoint_for checks both inputs before calling this.
std::string endpoint_name(const PeerIdentity& self, std::string_view root_hash16) {
    const std::optional<u32> uid = os_linux::ipc::parse_decimal_uid(self.user_id);
    if (!uid) return {};
    const NativePath runtime_base = os_linux::ipc::linux_ipc_runtime_base(*uid).path;
    return os_linux::ipc::engine_socket_path(runtime_base, root_hash16).string();
}

}  // namespace rb::ports
