#pragma once

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>

#include "reboot/foundation/diag.hpp"
#include "reboot/os_windows/ipc/pipe_trust.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_windows::ipc {

// Covers no capability ids; the reboot_client end of the engine pipe behind IIpcConnector.
// The returned stream reads on a thread it owns.
class NamedPipeConnector final : public ports::IIpcConnector {
public:
    explicit NamedPipeConnector(PipeTrust trust) : trust_(std::move(trust)) {}

    // Opened with SECURITY_IDENTIFICATION so the engine can identify this client but never act as it.
    // No pipe is platform.engine_not_listening and busy past `deadline` platform.pipe_connect_timed_out,
    // both EngineUnavailable so the client may autostart. A pipe this user may not open is
    // ipc.endpoint_untrusted caused by platform.pipe_access_denied. Nothing is written before verify_server;
    // its warning is only logged, since the engine reports its own elevation.
    Result<std::unique_ptr<ports::IByteStream>> connect(std::string_view endpoint_name,
                                                        std::chrono::milliseconds deadline) override;

private:
    PipeTrust trust_;
};

}  // namespace rb::os_windows::ipc
