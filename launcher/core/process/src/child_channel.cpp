#include "reboot/process/child_channel.hpp"

#include <optional>
#include <utility>

#include "messages.hpp"
#include "reboot/contracts/common.hpp"

namespace rb::process {

ChildChannel::ChildChannel(std::string program, ChildHandshake& handshake, Writer write, FrameSink on_frame)
    : program_(std::move(program)), handshake_(handshake), write_(std::move(write)), on_frame_(std::move(on_frame)) {}

Result<void> ChildChannel::feed(std::span<const u8> bytes) {
    if (phase_ == ChannelPhase::Closed) return {};
    std::optional<Diagnostic> error;
    const Framer::Status status = framer_.feed(bytes, [&](const RawFrame& frame) {
        Result<void> handled = on_raw_frame(frame);
        if (!handled) error = std::move(handled.error());
        return handled.has_value() && phase_ != ChannelPhase::Closed;
    });
    if (status == Framer::Status::ok) return {};
    phase_ = ChannelPhase::Closed;
    if (error) return std::unexpected(std::move(*error));
    if (status == Framer::Status::too_large)
        return make_diag(ErrorDomain::Process, msg::kChildFrameTooLarge)
            .arg("program", program_)
            .arg("limit", kChildFrameCap)
            .fail();
    return make_diag(ErrorDomain::Process, msg::kChildMalformedOutput).arg("program", program_).fail();
}

Result<void> ChildChannel::on_raw_frame(const RawFrame& frame) {
    if (phase_ == ChannelPhase::Open) {
        if (!is_frame<contracts::common::Ping>(frame)) {
            on_frame_(frame);
            return {};
        }
        Result<contracts::common::Ping> ping = decode_contract<contracts::common::Ping>(frame.payload);
        if (!ping) return std::unexpected(std::move(ping.error()));
        send(contracts::common::Pong{ping->nonce});
        return {};
    }

    if (frame.type != handshake_.hello_frame)
        return make_diag(ErrorDomain::Process, msg::kChildHelloExpected)
            .arg("program", program_)
            .arg("frame_type", frame.type)
            .fail();
    Result<u32> protocol = handshake_.read_protocol(frame.payload);
    if (!protocol) return std::unexpected(std::move(protocol.error()));
    if (*protocol != handshake_.expected_protocol)
        return make_diag(ErrorDomain::Process, msg::kChildProtocolMismatch)
            .arg("program", program_)
            .arg("actual", *protocol)
            .arg("expected", handshake_.expected_protocol)
            .fail();
    Result<std::vector<u8>> welcome = handshake_.welcome(frame.payload);
    if (!welcome) return std::unexpected(std::move(welcome.error()));
    phase_ = ChannelPhase::Open;
    write_(*welcome);
    return {};
}

void ChildChannel::send(std::span<const u8> frame) {
    if (phase_ == ChannelPhase::Open) write_(frame);
}

}  // namespace rb::process
