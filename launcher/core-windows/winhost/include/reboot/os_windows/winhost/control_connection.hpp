#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/os_windows/winhost/winhost_failure.hpp"
#include "wire/buffer.hpp"
#include "wire/codec.hpp"

namespace reboot::os_windows::winhost {

// One received frame. The payload may be a WhWelcome with -AUTH_PASSWORD and the game's
// REBOOT_CTL_TOKEN, so it is wiped on destruction.
struct ControlFrame {
    u64 type = 0;
    SecretBytes payload;

    [[nodiscard]] RawFrame raw() const noexcept { return {type, payload.reveal()}; }
};

// Covers no capability ids (decision game-control-channel).
// Blocking Winsock TCP client to the engine's game channel on 127.0.0.1, framed with
// kGameControlFrameCap. send() may be called from any thread; next_frame() only from the one
// reader thread. Nothing is read ahead, so no received byte outlives its ControlFrame. Owns its
// WSAStartup.
class ControlConnection {
public:
    // Connects to 127.0.0.1:<port> with TCP_NODELAY and writes the game-control preamble with
    // VersionStreams::payload_abi. FailureStep::Connect carries the WSA error.
    [[nodiscard]] static Expected<std::unique_ptr<ControlConnection>> connect(u16 port);

    ~ControlConnection();
    ControlConnection(const ControlConnection&) = delete;
    ControlConnection& operator=(const ControlConnection&) = delete;

    // Writes the whole frame under the send lock; FailureStep::Relay once the socket is gone.
    Expected<void> send(std::span<const u8> frame);

    // The encoded frame (a WhHello holds winhost's token) is wiped once written.
    template <ContractMessage T>
    Expected<void> send(const T& message) {
        sb::wire::Writer writer{kSendReserve};
        writer.quic_varint(contract_frame_type_v<T>);
        const std::size_t length_at = writer.reserve_len4();
        sb::wire::encode(writer, message);
        writer.patch_len4(length_at);
        const SecretBytes frame{writer.take()};
        return send(std::span<const u8>{frame.reveal()});
    }

    // Reads the varint type, the varint length (at most kGameControlFrameCap) and exactly that
    // many payload bytes. nullopt on EOF; FailureStep::Protocol for an oversized or malformed
    // header, FailureStep::Relay for a socket error.
    [[nodiscard]] Expected<std::optional<ControlFrame>> next_frame();

    // Shuts the socket down both ways, so next_frame() returns EOF and the engine sees a
    // disconnect; called once a Stop has emptied the Job.
    void shutdown() noexcept;

private:
    // Holds every frame but an Output chunk, so encoding a frame with a secret never reallocates
    // and leaves an unwiped copy behind.
    static constexpr std::size_t kSendReserve = std::size_t{4} << 10;

    explicit ControlConnection(std::uintptr_t socket) noexcept;

    std::uintptr_t socket_;
    std::mutex send_mutex_;
    std::atomic<bool> shut_{false};
};

}  // namespace reboot::os_windows::winhost
