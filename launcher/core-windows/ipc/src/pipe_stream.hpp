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
// Reads and on_close run on the bound PipeIoThread; write() queues and is thread-safe.
class PipeStream final : public ports::IByteStream {
public:
    // Engine side: `io` is the listener's thread, which outlives the stream or closes it first.
    [[nodiscard]] static Result<std::unique_ptr<PipeStream>> accepted(UniqueHandle pipe, ports::PeerIdentity peer,
                                                                      PipeIoThread& io);
    // Client side: the stream owns its own I/O thread.
    [[nodiscard]] static Result<std::unique_ptr<PipeStream>> connected(UniqueHandle pipe, ports::PeerIdentity peer);

    ~PipeStream() override;

    void write(std::span<const u8> bytes) override;
    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override;
    void on_close(UniqueFunction<void()> callback) override;
    // DisconnectNamedPipe on the engine side, then closes the handle; on_close fires once.
    void close() override;
    [[nodiscard]] ports::PeerIdentity peer() const override;

private:
    PipeStream();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::os_windows::ipc
