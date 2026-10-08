#include "reboot/os_linux/ipc/ipc_runtime_base.hpp"

#include <cstdlib>
#include <string>

namespace reboot::os_linux::ipc {

IpcRuntimeBase resolve_ipc_runtime_base(std::optional<std::string_view> xdg_runtime_dir, u32 uid) {
    if (xdg_runtime_dir && xdg_runtime_dir->starts_with('/')) return {NativePath{*xdg_runtime_dir}, true};
    return {NativePath{"/tmp/reboot-launcher-" + std::to_string(uid)}, false};
}

IpcRuntimeBase linux_ipc_runtime_base(u32 uid) {
    const char* const xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir == nullptr) return resolve_ipc_runtime_base(std::nullopt, uid);
    return resolve_ipc_runtime_base(std::string_view{xdg_runtime_dir}, uid);
}

}  // namespace reboot::os_linux::ipc
