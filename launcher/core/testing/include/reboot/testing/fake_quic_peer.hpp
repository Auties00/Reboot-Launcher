#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ports/net.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// The server's side of one connection opened through FakeQuicTransport. Callbacks reach the client
// through `deliver_on`, and nothing after the client closed or dropped its IQuicConnection.
class FakeQuicPeer {
public:
    FakeQuicPeer(Executor& deliver_on, ports::QuicConnectOptions options, ports::QuicCallbacks callbacks);
    ~FakeQuicPeer();
    FakeQuicPeer(const FakeQuicPeer&) = delete;
    FakeQuicPeer& operator=(const FakeQuicPeer&) = delete;

    [[nodiscard]] std::unique_ptr<ports::IQuicConnection> make_handle();
    [[nodiscard]] const ports::QuicConnectOptions& options() const noexcept { return options_; }

    void accept();
    // on_closed with `error` and no on_connected, as a refused handshake.
    void refuse(Diagnostic error);
    void send(u64 stream_id, std::vector<u8> bytes, bool fin);
    void send_datagram(std::vector<u8> bytes);
    void close(std::optional<Diagnostic> error);

    // Run on the client's thread as it sends, after the bytes are recorded, e.g. to answer at once.
    void on_stream_data(UniqueFunction<void(u64 stream_id, std::span<const u8> bytes, bool fin)> handler);
    void on_datagram(UniqueFunction<void(std::span<const u8> bytes)> handler);

    // Client-opened stream ids: 0, 4, 8 and so on, as QUIC numbers client bidirectional streams.
    [[nodiscard]] std::vector<u64> streams() const;
    // Everything sent on `stream_id`, concatenated.
    [[nodiscard]] std::vector<u8> received(u64 stream_id) const;
    [[nodiscard]] bool fin_received(u64 stream_id) const;
    [[nodiscard]] const std::vector<std::vector<u8>>& datagrams() const noexcept { return datagrams_; }
    [[nodiscard]] std::optional<u64> closed_with() const noexcept { return closed_with_; }
    [[nodiscard]] bool released() const noexcept { return released_; }

private:
    struct Stream;
    struct Delivery;

    Executor& deliver_on_;
    ports::QuicConnectOptions options_;
    std::shared_ptr<Delivery> delivery_;
    std::vector<std::unique_ptr<Stream>> streams_;
    std::vector<std::vector<u8>> datagrams_;
    std::optional<u64> closed_with_;
    u64 next_stream_id_ = 0;
    bool released_ = false;
};

}  // namespace reboot::testing
