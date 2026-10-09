#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_paths.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

#include "velopack_layout.hpp"
#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

constexpr const wchar_t* kAppFolder = L"Reboot Launcher";

[[nodiscard]] Result<NativePath> local_app_data() {
    wchar_t* folder = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(hr)) {
        CoTaskMemFree(folder);
        return std::unexpected(hresult_failed("SHGetKnownFolderPath", hr));
    }
    NativePath path(folder);
    CoTaskMemFree(folder);
    return NativePath(shell_path(path));
}

[[nodiscard]] Result<NativePath> running_exe() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::unexpected(call_failed("GetModuleFileNameW", GetLastError()));
        if (length < buffer.size()) return NativePath(shell_path(NativePath(std::wstring(buffer.data(), length))));
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace

Result<WindowsPaths> WindowsPaths::detect() {
    auto app_data = local_app_data();
    if (!app_data) return std::unexpected(std::move(app_data.error()));
    auto exe = running_exe();
    if (!exe) return std::unexpected(std::move(exe.error()));
    NativePath exe_dir = exe->parent_path();
    std::optional<NativePath> velopack_root = velopack_root_of(exe_dir, [](const NativePath& file) {
        std::error_code error;
        return std::filesystem::is_regular_file(file, error);
    });
    return WindowsPaths(std::move(*app_data), std::move(exe_dir), std::move(velopack_root));
}

WindowsPaths::WindowsPaths(NativePath local_app_data, NativePath exe_dir, std::optional<NativePath> velopack_root)
    : local_app_data_(std::move(local_app_data)), exe_dir_(std::move(exe_dir)), velopack_root_(std::move(velopack_root)) {}

NativePath WindowsPaths::default_data_root() const { return local_app_data_ / kAppFolder; }

NativePath WindowsPaths::default_cache_root() const { return default_data_root() / "cache"; }

NativePath WindowsPaths::default_logs_root() const { return default_data_root() / "logs"; }

NativePath WindowsPaths::ipc_runtime_base() const { return {}; }

NativePath WindowsPaths::exe_dir() const { return exe_dir_; }

ports::InstallKind WindowsPaths::install_kind() const {
    return velopack_root_ ? ports::InstallKind::Velopack : ports::InstallKind::Portable;
}

std::optional<NativePath> WindowsPaths::velopack_package_dir() const { return velopack_root_; }

}  // namespace rb::os_windows::platform
