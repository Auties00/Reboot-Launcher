#include "reboot/os_windows/ipc/named_pipe_connector.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "messages.hpp"
#include "pipe_stream.hpp"
#include "reboot/foundation/log.hpp"
#include "unique_handle.hpp"
#include "wide.hpp"
#include "win32.hpp"
#include "win32_errors.hpp"

namespace reboot::os_windows::ipc {

Result<std::unique_ptr<ports::IByteStream>> NamedPipeConnector::connect(std::string_view endpoint_name,
                                                                        std::chrono::milliseconds deadline) {
    const std::wstring name = to_wide(endpoint_name);
    const auto give_up = std::chrono::steady_clock::now() + deadline;
    UniqueHandle pipe;
    while (!pipe) {
        pipe = UniqueHandle{CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr)};
        if (pipe) break;
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            return std::unexpected(make_diag(ErrorDomain::Platform, kEngineNotListening)
                                       .arg("name", endpoint_name)
                                       .kind(ErrorKind::EngineUnavailable)
                                       .retryable()
                                       .build());
        // Only the engine's user and SYSTEM may open its pipe.
        if (error == ERROR_ACCESS_DENIED)
            return std::unexpected(untrusted(make_diag(ErrorDomain::Platform, kPipeAccessDenied).arg("name", endpoint_name)));
        if (error != ERROR_PIPE_BUSY) return std::unexpected(pipe_call_failed("CreateFileW", endpoint_name, error));

        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(give_up - std::chrono::steady_clock::now());
        if (left.count() <= 0)
            return std::unexpected(make_diag(ErrorDomain::Platform, kPipeConnectTimedOut)
                                       .arg("name", endpoint_name)
                                       .arg("deadline", deadline)
                                       .kind(ErrorKind::EngineUnavailable)
                                       .retryable()
                                       .build());
        // 0 would mean the server's default wait, so at least 1 ms.
        const auto wait = static_cast<DWORD>(std::clamp<long long>(left.count(), 1, 0x7FFFFFFF));
        if (!WaitNamedPipeW(name.c_str(), wait) && GetLastError() == ERROR_FILE_NOT_FOUND)
            return std::unexpected(make_diag(ErrorDomain::Platform, kEngineNotListening)
                                       .arg("name", endpoint_name)
                                       .kind(ErrorKind::EngineUnavailable)
                                       .retryable()
                                       .build());
    }

    Result<VerifiedServer> server = trust_.verify_server(pipe.get());
    if (!server) return std::unexpected(std::move(server.error()));
    if (server->warning) REBOOT_LOG_WARN(Ipc, "engine pipe {}: {}", endpoint_name, server->warning->id);
    Result<std::unique_ptr<PipeStream>> stream = PipeStream::connected(std::move(pipe), std::move(server->peer));
    if (!stream) return std::unexpected(std::move(stream.error()));
    return std::unique_ptr<ports::IByteStream>(std::move(*stream));
}

}  // namespace reboot::os_windows::ipc
