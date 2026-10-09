#include <string>
#include <string_view>

#include "darwin_user_temp_dir.hpp"
#include "engine_socket_path.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::ports {

// `self` is not part of the path: the temp directory is already per-user. ipc::endpoint_for
// checks both inputs before calling this.
std::string endpoint_name(const PeerIdentity& /*self*/, std::string_view root_hash16) {
    const Result<NativePath> user_temp_dir = os_macos::ipc::darwin_user_temp_dir();
    if (!user_temp_dir) return {};
    return os_macos::ipc::engine_socket_path(*user_temp_dir, root_hash16).string();
}

}  // namespace rb::ports
