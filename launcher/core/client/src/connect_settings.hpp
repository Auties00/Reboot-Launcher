#pragma once

#include <chrono>
#include <optional>

#include "reboot/client.h"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/ipc_client.hpp"

namespace reboot::client {

// rb_ctx_options, validated and copied, so nothing borrowed outlives rb_ctx_create.
struct ConnectSettings {
    std::optional<NativePath> data_root;
    contracts::ipc::ClientKind client_kind{};
    ipc::LaunchMode launch_mode = ipc::LaunchMode::Autostart;
    std::chrono::milliseconds connect_deadline = default_deadline(OpKind::EngineConnect);
};

// The rb_ctx_options layout of ABI 1.0; a smaller struct_size is invalid.
inline constexpr u32 kOptionsSizeV1_0 = sizeof(rb_ctx_options);

// client.abi_mismatch when bytes past the 1.0 layout are not zero: the caller needs a newer minor.
[[nodiscard]] Result<ConnectSettings> read_connect_settings(const rb_ctx_options* options);

static_assert(RB_CLIENT_UNKNOWN == static_cast<int>(contracts::ipc::ClientKind::Unknown));
static_assert(RB_CLIENT_WINDOWS_GUI == static_cast<int>(contracts::ipc::ClientKind::WindowsGui));
static_assert(RB_CLIENT_MAC_GUI == static_cast<int>(contracts::ipc::ClientKind::MacGui));
static_assert(RB_CLIENT_LINUX_GUI == static_cast<int>(contracts::ipc::ClientKind::LinuxGui));
static_assert(RB_CLIENT_CLI == static_cast<int>(contracts::ipc::ClientKind::Cli));
static_assert(RB_CLIENT_TEST == static_cast<int>(contracts::ipc::ClientKind::Test));
static_assert(RB_LAUNCH_AUTOSTART == static_cast<int>(ipc::LaunchMode::Autostart));
static_assert(RB_LAUNCH_CONNECT_ONLY == static_cast<int>(ipc::LaunchMode::ConnectOnly));

}  // namespace reboot::client
