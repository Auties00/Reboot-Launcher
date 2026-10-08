#pragma once

#include <optional>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/ipc_codec.hpp"

namespace reboot::ipc {

// A completed Hello/HelloAck exchange.
struct Handshake {
    contracts::ipc::HelloAck hello;
    // ipc.engine_image_differs when the engine runs from another image than engine_exe.
    std::optional<Diagnostic> image_warning;
};

enum class LinkLoss : u8 { Reconnecting, Final };

// Covers no capability ids. Where IpcClient delivers what the engine sends; reboot_client's
// ClientContext implements it. Called from the reader thread or the executor, never under a lock.
class IIpcClientSink {
public:
    virtual ~IIpcClientSink() = default;

    // Every frame after the handshake, including a later HelloAck when secrets_available changes.
    virtual void on_frame(EngineMessage message) = 0;
    // ipc.connection_lost, ipc.engine_closed after a Goodbye, or ipc.root_mismatch (always Final).
    virtual void on_lost(const Diagnostic& reason, LinkLoss loss) = 0;
    // Compare the epoch, then resubscribe and reattach, or fail the old epoch's ops.
    virtual void on_reconnected(const Handshake& handshake) = 0;
};

}  // namespace reboot::ipc
