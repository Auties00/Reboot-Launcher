#pragma once

#include <span>
#include <string>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/process/child_handshake.hpp"

namespace reboot::process {

enum class ChannelPhase : u8 { AwaitingHello, Open, Closed };

// Covers no capability ids; the engine end of the backend and game-server stdio contracts.
// Strand-only. Frames stdout with kChildFrameCap and requires the role's Hello as the first frame,
// with an exact protocol match (process.child_protocol_mismatch), then sends the role's Welcome.
// Answers the child's Ping with Pong. Any error closes the channel and the supervisor kills the child.
class ChildChannel {
public:
    // Writes to the child's stdin.
    using Writer = UniqueFunction<void(std::span<const u8>)>;
    // Every frame after Welcome except the child's Ping; the payload is valid during the call only.
    using FrameSink = UniqueFunction<void(const RawFrame&)>;

    // `program` names the child in every error this channel returns.
    ChildChannel(std::string program, ChildHandshake& handshake, Writer write, FrameSink on_frame);

    // stdout bytes. An error leaves the channel Closed; phase() before the call tells a failed
    // handshake from a protocol error after it.
    Result<void> feed(std::span<const u8> bytes);

    // Dropped unless Open.
    void send(std::span<const u8> frame);
    template <ContractMessage T>
    void send(const T& message) {
        send(encode_contract_frame(message));
    }

    [[nodiscard]] ChannelPhase phase() const noexcept { return phase_; }

private:
    [[nodiscard]] Result<void> on_raw_frame(const RawFrame& frame);

    std::string program_;
    ChildHandshake& handshake_;
    Writer write_;
    FrameSink on_frame_;
    Framer framer_{kChildFrameCap};
    ChannelPhase phase_ = ChannelPhase::AwaitingHello;
};

}  // namespace reboot::process
