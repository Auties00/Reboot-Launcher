#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace rb::os_macos::ipc {

inline constexpr std::string_view kSocketDirName = "reboot-launcher";

// <user_temp_dir>/reboot-launcher/<root_hash16>.sock, never under /tmp; empty when
// `user_temp_dir` is, which check_engine_socket_path rejects.
[[nodiscard]] NativePath engine_socket_path(const NativePath& user_temp_dir, std::string_view root_hash16);

// `endpoint_name` must be exactly engine_socket_path(user_temp_dir, <16 lowercase hex digits>),
// otherwise ipc.endpoint_untrusted caused by platform.endpoint_outside_user_temp.
[[nodiscard]] Result<NativePath> check_engine_socket_path(const NativePath& user_temp_dir,
                                                          std::string_view endpoint_name);

}  // namespace rb::os_macos::ipc
