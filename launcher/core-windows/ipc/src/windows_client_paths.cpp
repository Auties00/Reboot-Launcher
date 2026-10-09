#include "reboot/os_windows/ipc/windows_client_paths.hpp"

#include "win32.hpp"

#include <knownfolders.h>
#include <shlobj.h>

#include <string>
#include <utility>

#include "reboot/foundation/paths.hpp"
#include "win32_errors.hpp"

namespace reboot::os_windows::ipc {
namespace {

// Lives in the module holding this code: reboot_client.dll for clients.
constinit const char kModuleAnchor = 0;

[[nodiscard]] Result<NativePath> this_module_path() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&kModuleAnchor), &module))
        return std::unexpected(call_failed("GetModuleHandleExW"));
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return std::unexpected(call_failed("GetModuleFileNameW"));
        if (length < path.size()) {
            path.resize(length);
            return NativePath{path};
        }
        path.resize(path.size() * 2);
    }
}

}  // namespace

Result<WindowsClientPaths> WindowsClientPaths::detect() {
    wchar_t* folder = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(result)) {
        CoTaskMemFree(folder);
        return std::unexpected(call_failed("SHGetKnownFolderPath", static_cast<DWORD>(result)));
    }
    NativePath local_app_data = NativePath{folder}.lexically_normal();
    CoTaskMemFree(folder);
    Result<NativePath> module = this_module_path();
    if (!module) return std::unexpected(std::move(module.error()));
    NativePath exe_dir = module->parent_path().lexically_normal();
    std::optional<NativePath> velopack_root = velopack_root_of(exe_dir);
    return WindowsClientPaths{std::move(local_app_data), std::move(exe_dir), std::move(velopack_root)};
}

WindowsClientPaths::WindowsClientPaths(NativePath local_app_data, NativePath exe_dir, std::optional<NativePath> velopack_root)
    : local_app_data_(std::move(local_app_data)), exe_dir_(std::move(exe_dir)), velopack_root_(std::move(velopack_root)) {}

NativePath WindowsClientPaths::default_data_root() const { return local_app_data_ / "Reboot Launcher"; }

NativePath WindowsClientPaths::default_cache_root() const { return default_data_root() / "cache"; }

NativePath WindowsClientPaths::default_logs_root() const { return default_data_root() / "logs"; }

NativePath WindowsClientPaths::ipc_runtime_base() const { return {}; }

NativePath WindowsClientPaths::exe_dir() const { return exe_dir_; }

ports::InstallKind WindowsClientPaths::install_kind() const {
    return velopack_root_ ? ports::InstallKind::Velopack : ports::InstallKind::Portable;
}

std::optional<NativePath> WindowsClientPaths::velopack_package_dir() const { return velopack_root_; }

}  // namespace reboot::os_windows::ipc
