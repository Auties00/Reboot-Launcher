#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"
#include "reboot/posix/unique_fd.hpp"
#include "socket_fds.hpp"

namespace reboot::posix {

// IByteStream over one connected AF_UNIX socket whose peer PeerCredentialCheck already verified.
// Reading and on_close run on a thread the stream owns; write() and close() are thread-safe.
// Callbacks only post elsewhere: destroying the stream inside one would join its own thread.
class SocketStream final : public ports::IByteStream {
public:
    // `socket` comes from make_unix_stream_socket or accept_unix_stream.
    [[nodiscard]] static Result<std::unique_ptr<SocketStream>> start(UniqueFd socket, ports::PeerIdentity peer);

    ~SocketStream() override;
    SocketStream(const SocketStream&) = delete;
    SocketStream& operator=(const SocketStream&) = delete;

    // Sends at once when nothing is queued; the rest waits for the stream's thread.
    void write(std::span<const u8> bytes) override;
    void on_read(UniqueFunction<void(std::span<const u8>)> callback) override;
    void on_close(UniqueFunction<void()> callback) override;
    // Sends what the socket takes without waiting, then shuts it down; on_close fires once.
    void close() override;
    [[nodiscard]] ports::PeerIdentity peer() const override;

private:
    SocketStream(UniqueFd socket, ports::PeerIdentity peer, WakePipe wake);

    void run();
    // Returns false once the thread should exit.
    [[nodiscard]] bool run_once();
    void read_available();
    void fire_close_once();
    // Sends without blocking and returns the bytes sent. A peer that hung up only stops sending,
    // so what it sent first is still read; any other failure finishes the stream.
    [[nodiscard]] std::size_t send_locked(std::span<const u8> bytes) noexcept;
    void flush_locked() noexcept;
    void finish_locked() noexcept;

    UniqueFd socket_;
    const ports::PeerIdentity peer_;
    WakePipe wake_;

    std::mutex mutex_;
    std::vector<u8> outbound_;
    // Bytes at the front of outbound_ already sent; compacted lazily, since a small socket buffer
    // would otherwise move a large write once per send.
    std::size_t outbound_sent_ = 0;
    UniqueFunction<void(std::span<const u8>)> on_read_;
    UniqueFunction<void()> on_close_;
    // Nothing more is read or sent; on_close is due.
    bool finished_ = false;
    // A send hit EPIPE or ECONNRESET: writes are dropped while reading runs to EOF.
    bool send_closed_ = false;
    bool close_fired_ = false;
    // The peer hung up before on_read was set, so the socket is not polled until it is.
    bool hung_up_ = false;
    bool stopping_ = false;

    // Stream thread only.
    std::vector<u8> read_buffer_;
    std::thread thread_;
};

}  // namespace reboot::posix
