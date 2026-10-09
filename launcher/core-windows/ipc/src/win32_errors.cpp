#include "win32_errors.hpp"

#include <utility>

#include "messages.hpp"

namespace reboot::os_windows::ipc {
namespace {

[[nodiscard]] SystemError host_error(DWORD error) { return SystemError{SystemError::Origin::Host, static_cast<i64>(error)}; }

}  // namespace

Diagnostic call_failed(std::string_view call, DWORD error) {
    return make_diag(ErrorDomain::Platform, kIpcCallFailed).arg("call", call).os(host_error(error));
}

Diagnostic call_failed(std::string_view call) { return call_failed(call, GetLastError()); }

Diagnostic call_failed_on_path(std::string_view call, const NativePath& path, DWORD error) {
    return make_diag(ErrorDomain::Platform, kIpcCallFailedOnPath).arg("call", call).arg("path", path).os(host_error(error));
}

Diagnostic pipe_call_failed(std::string_view call, std::string_view name, DWORD error) {
    return make_diag(ErrorDomain::Platform, kPipeCallFailed).arg("call", call).arg("name", name).os(host_error(error));
}

Diagnostic untrusted(Diagnostic cause) {
    return make_diag(ErrorDomain::Ipc, kEndpointUntrusted).cause(std::move(cause));
}

}  // namespace reboot::os_windows::ipc
