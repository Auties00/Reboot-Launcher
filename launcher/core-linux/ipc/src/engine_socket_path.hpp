#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot::os_linux::ipc {

inline constexpr std::string_view kSocketDirName = "reboot-launcher";

// <runtime_base>/reboot-launcher/<root_hash16>.sock; empty when `runtime_base` is, which
// check_engine_socket_path rejects.
[[nodiscard]] NativePath engine_socket_path(const NativePath& runtime_base, std::string_view root_hash16);

// `endpoint_name` must be exactly engine_socket_path(runtime_base, <16 lowercase hex digits>),
// otherwise ipc.endpoint_untrusted caused by platform.ipc_endpoint_outside_runtime_dir.
[[nodiscard]] Result<NativePath> check_engine_socket_path(const NativePath& runtime_base,
                                                          std::string_view endpoint_name);

}  // namespace reboot::os_linux::ipc
