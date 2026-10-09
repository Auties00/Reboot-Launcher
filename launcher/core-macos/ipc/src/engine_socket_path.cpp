#include "engine_socket_path.hpp"

#include <string>

#include "messages.hpp"

namespace rb::os_macos::ipc {
namespace {

constexpr std::string_view kSocketSuffix = ".sock";

[[nodiscard]] bool is_root_hash16(std::string_view text) noexcept {
    if (text.size() != 16) return false;
    for (const char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

}  // namespace

NativePath engine_socket_path(const NativePath& user_temp_dir, std::string_view root_hash16) {
    if (user_temp_dir.empty()) return {};
    std::string file_name(root_hash16);
    file_name += kSocketSuffix;
    return user_temp_dir / kSocketDirName / file_name;
}

Result<NativePath> check_engine_socket_path(const NativePath& user_temp_dir, std::string_view endpoint_name) {
    const NativePath socket{endpoint_name};
    if (!user_temp_dir.empty() && endpoint_name.ends_with(kSocketSuffix)) {
        const std::string name = socket.filename().string();
        const std::string_view stem = std::string_view(name).substr(0, name.size() - kSocketSuffix.size());
        // Rebuilding from the stem rejects '.', '..' and any other parent.
        if (is_root_hash16(stem) && engine_socket_path(user_temp_dir, stem) == socket) return socket;
    }
    return make_diag(ErrorDomain::Ipc, posix::kEndpointUntrusted)
        .cause(make_diag(ErrorDomain::Platform, kEndpointOutsideUserTemp)
                   .arg("path", socket)
                   .arg("expected_dir", user_temp_dir.empty() ? NativePath{} : user_temp_dir / kSocketDirName))
        .fail();
}

}  // namespace rb::os_macos::ipc
