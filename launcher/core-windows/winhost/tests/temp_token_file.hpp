#pragma once

#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include <array>
#include <atomic>
#include <string>
#include <string_view>

namespace rb::os_windows::winhost::test {

// `contents` in <temp>\reboot-winhost-test-<pid>-<n>\token, as the engine writes winhost's token
// file; the file and its directory go with this.
class TempTokenFile {
public:
    explicit TempTokenFile(std::string_view contents) {
        static std::atomic<unsigned> next{0};
        std::array<wchar_t, MAX_PATH + 1> temp{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
        dir_ = std::wstring(temp.data(), length) + L"reboot-winhost-test-" + std::to_wstring(GetCurrentProcessId()) +
               L"-" + std::to_wstring(next++);
        CreateDirectoryW(dir_.c_str(), nullptr);
        path_ = dir_ + L"\\token";
        const HANDLE file =
            CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
        CloseHandle(file);
    }

    ~TempTokenFile() {
        DeleteFileW(path_.c_str());
        RemoveDirectoryW(dir_.c_str());
    }

    TempTokenFile(const TempTokenFile&) = delete;
    TempTokenFile& operator=(const TempTokenFile&) = delete;

    [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

private:
    std::wstring dir_;
    std::wstring path_;
};

}  // namespace rb::os_windows::winhost::test
