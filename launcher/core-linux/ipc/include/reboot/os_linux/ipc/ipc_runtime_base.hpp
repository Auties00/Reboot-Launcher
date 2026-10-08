#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::os_linux::ipc {

// The directory under which the engine socket lives, as reboot-launcher/<hash16>.sock.
struct IpcRuntimeBase {
    NativePath path;
    // False for the /tmp fallback, which no systemd unit's %t names.
    bool from_xdg_runtime_dir = false;
};

// `xdg_runtime_dir` when it is absolute, otherwise /tmp/reboot-launcher-<uid>.
[[nodiscard]] IpcRuntimeBase resolve_ipc_runtime_base(std::optional<std::string_view> xdg_runtime_dir, u32 uid);

// resolve_ipc_runtime_base over this process's $XDG_RUNTIME_DIR. Must equal
// os_linux::platform::XdgPaths::ipc_runtime_base(). Listener and connector lstat-check it
// (owner `uid`, mode 0700) before use.
[[nodiscard]] IpcRuntimeBase linux_ipc_runtime_base(u32 uid);

}  // namespace reboot::os_linux::ipc
