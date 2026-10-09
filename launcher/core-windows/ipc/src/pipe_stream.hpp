#pragma once

#include <memory>
#include <span>

#include "pipe_io_thread.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"
#include "unique_handle.hpp"

namespace reboot::os_windows::ipc {

// IByteStream over one connected, overlapped pipe handle whose peer PipeTrust already verified.
// Reads and on_close run on the bound PipeIoThread; write() and close() are thread-safe. Reading
// starts once on_read is set. Destruction waits for a callback running on another thread.
class PipeStream final : public ports::IByteStream {
public:
    // Engine side: `io` is the listener's thread, which outlives the stream or closes it first.
    [[nodiscard]] static Result<std::unique_ptr<PipeStream>> accepted(UniqueHandle pipe, ports::PeerIdentity peer,
                                                                      PipeIoThread& io);
    // Client side: the stream owns its own I/O thread, so it is not destroyed from its callbacks.
    [[nodiscard]] static Result<std::unique_ptr<PipeStream>> connected(UniqueHandle pipe, ports::PeerIdentity peer);

    ~PipeStream() override;
    PipeStream(const PipeStream&) = delete;
    PipeStream& operator=(const PipeStream&) = delete;

    void write(std::span<const u8> bytes) override;
    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override;
    void on_close(UniqueFunction<void()> callback) override;
    // Hands the queued bytes to the pipe, cancels what it cannot take at once and closes the
    // handle without DisconnectNamedPipe, so bytes the peer has not read yet stay readable;
    // on_close fires once on each end. A peer that hung up only stops writes; reading runs on.
    void close() override;
    [[nodiscard]] ports::PeerIdentity peer() const override;

private:
    struct Impl;

    PipeStream(std::shared_ptr<Impl> impl, std::unique_ptr<PipeIoThread> owned_io);

    // Shared with the packets in flight, which keep it alive until they complete.
    std::shared_ptr<Impl> impl_;
    std::unique_ptr<PipeIoThread> owned_io_;
};

}  // namespace reboot::os_windows::ipc
