#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "reboot/os_windows/platform/windows_disk_info.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>

#include "wide.hpp"
#include "win_error.hpp"

namespace rb::os_windows::platform {

namespace {

// nullopt for a drive that is not ready, such as an empty card reader.
[[nodiscard]] Result<std::optional<ports::VolumeInfo>> describe(const std::wstring& mount) {
    ports::VolumeInfo info;
    info.mount = NativePath(mount);
    const UINT type = GetDriveTypeW(mount.c_str());
    if (type == DRIVE_NO_ROOT_DIR) return std::optional<ports::VolumeInfo>{};
    info.removable = type == DRIVE_REMOVABLE || type == DRIVE_CDROM;
    info.network = type == DRIVE_REMOTE;

    std::array<wchar_t, MAX_PATH + 1> label{};
    std::array<wchar_t, MAX_PATH + 1> fs_name{};
    DWORD flags = 0;
    if (GetVolumeInformationW(mount.c_str(), label.data(), static_cast<DWORD>(label.size()), nullptr, nullptr, &flags,
                              fs_name.data(), static_cast<DWORD>(fs_name.size())) == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_READY || error == ERROR_UNRECOGNIZED_VOLUME || error == ERROR_INVALID_PARAMETER)
            return std::optional<ports::VolumeInfo>{};
        return std::unexpected(call_failed("GetVolumeInformationW", error, info.mount));
    }
    info.label = narrow(label.data());
    info.fs_type = narrow(fs_name.data());
    info.read_only = (flags & FILE_READ_ONLY_VOLUME) != 0;

    ULARGE_INTEGER free_to_caller{};
    ULARGE_INTEGER total{};
    if (GetDiskFreeSpaceExW(mount.c_str(), &free_to_caller, &total, nullptr) == 0) {
        const DWORD error = GetLastError();
        if (error == ERROR_NOT_READY) return std::optional<ports::VolumeInfo>{};
        return std::unexpected(call_failed("GetDiskFreeSpaceExW", error, info.mount));
    }
    info.free_bytes = free_to_caller.QuadPart;
    info.total_bytes = total.QuadPart;
    return std::optional<ports::VolumeInfo>{std::move(info)};
}

// GetVolumePathNameW resolves an existing path best, so a destination not made yet uses its
// nearest existing ancestor.
[[nodiscard]] NativePath existing_ancestor(const NativePath& path) {
    NativePath current = NativePath(shell_path(path));
    std::error_code error;
    while (current.has_relative_path() && !std::filesystem::exists(current, error)) current = current.parent_path();
    return current;
}

}  // namespace

Result<std::vector<ports::VolumeInfo>> WindowsDiskInfo::volumes() {
    std::vector<wchar_t> buffer(256);
    for (;;) {
        const DWORD needed = GetLogicalDriveStringsW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (needed == 0) return std::unexpected(call_failed("GetLogicalDriveStringsW", GetLastError()));
        if (needed < buffer.size()) break;
        buffer.resize(needed + 1);
    }
    std::vector<ports::VolumeInfo> volumes;
    for (const wchar_t* root = buffer.data(); *root != L'\0'; root += std::wcslen(root) + 1) {
        // One unreadable drive, such as a mapped share that is offline, must not hide the others.
        if (auto volume = describe(root); volume && *volume) volumes.push_back(std::move(**volume));
    }
    return volumes;
}

Result<ports::VolumeInfo> WindowsDiskInfo::volume_of(const NativePath& path) {
    const std::wstring target = shell_path(existing_ancestor(path));
    std::vector<wchar_t> mount(target.size() + MAX_PATH + 2);
    if (GetVolumePathNameW(target.c_str(), mount.data(), static_cast<DWORD>(mount.size())) == 0)
        return std::unexpected(call_failed("GetVolumePathNameW", GetLastError(), path));
    auto volume = describe(mount.data());
    if (!volume) return std::unexpected(std::move(volume.error()));
    if (!*volume) return std::unexpected(call_failed("GetVolumeInformationW", ERROR_NOT_READY, path));
    return std::move(**volume);
}

}  // namespace rb::os_windows::platform
