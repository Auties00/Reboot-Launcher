#pragma once

#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/ports/process.hpp"
#include "reboot/process/env_layer.hpp"
#include "reboot/process/process_spec.hpp"

namespace reboot::backend {

inline constexpr std::string_view kBackendDataEnv = "REBOOT_BACKEND_DATA";
inline constexpr std::string_view kBackendContentEnv = "REBOOT_BACKEND_CONTENT";
inline constexpr std::string_view kBackendControlArg = "--control=stdio";

struct BackendLaunch {
    // InstallLayout::backend_exe.
    NativePath exe;
    // InstallLayout::backend_content_dir; read-only.
    NativePath content_dir;
    // AppLayout::backend_dir; also the cwd.
    NativePath data_dir;
    ports::EnvBlock user_environment;
    process::EnvSyntax env_syntax = process::EnvSyntax::Posix;
};

// `<exe> --control=stdio` in `data_dir`, with the EnvBuilder base plus REBOOT_BACKEND_DATA and
// REBOOT_BACKEND_CONTENT.
[[nodiscard]] Result<process::ProcessSpec> make_backend_spec(const BackendLaunch& launch);

}  // namespace reboot::backend
