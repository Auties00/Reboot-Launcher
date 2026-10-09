#include "win32.hpp"  // first: sets _WIN32_WINNT before any std header pulls in <windows.h>

#include "win_error.hpp"

#include "messages.hpp"

namespace rb::os_windows::platform {

namespace {

[[nodiscard]] SystemError host_error(i64 code) noexcept { return SystemError{SystemError::Origin::Host, code}; }

[[nodiscard]] bool retryable_win32(u32 error) noexcept {
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
}

[[nodiscard]] u32 win32_of(i32 hr) noexcept {
    const auto bits = static_cast<u32>(hr);
    return (bits & 0xFFFF0000u) == 0x80070000u ? (bits & 0xFFFFu) : 0;
}

}  // namespace

ErrorKind kind_of_win32(u32 error) noexcept {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE:
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_NET_NAME:
        case ERROR_NOT_FOUND:
        case ERROR_INVALID_NAME:
            return ErrorKind::NotFound;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            return ErrorKind::Conflict;
        default:
            return ErrorKind::Generic;
    }
}

ErrorKind kind_of_hresult(i32 hr) noexcept {
    const u32 win32 = win32_of(hr);
    return win32 != 0 ? kind_of_win32(win32) : ErrorKind::Generic;
}

Diagnostic call_failed(std::string_view call, u32 error) {
    return make_diag(ErrorDomain::Platform, kCallFailed)
        .arg("call", call)
        .os(host_error(error))
        .kind(kind_of_win32(error))
        .retryable(retryable_win32(error));
}

Diagnostic call_failed(std::string_view call, u32 error, const NativePath& path) {
    return make_diag(ErrorDomain::Platform, kCallFailedOnPath)
        .arg("call", call)
        .arg("path", path)
        .os(host_error(error))
        .kind(kind_of_win32(error))
        .retryable(retryable_win32(error));
}

Diagnostic hresult_failed(std::string_view call, i32 hr) {
    return make_diag(ErrorDomain::Platform, kCallFailed)
        .arg("call", call)
        .os(host_error(hr))
        .kind(kind_of_hresult(hr))
        .retryable(retryable_win32(win32_of(hr)));
}

Diagnostic hresult_failed(std::string_view call, i32 hr, const NativePath& path) {
    return make_diag(ErrorDomain::Platform, kCallFailedOnPath)
        .arg("call", call)
        .arg("path", path)
        .os(host_error(hr))
        .kind(kind_of_hresult(hr))
        .retryable(retryable_win32(win32_of(hr)));
}

}  // namespace rb::os_windows::platform
