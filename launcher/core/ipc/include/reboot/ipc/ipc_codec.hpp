#pragma once

#include <cstddef>
#include <span>
#include <variant>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/ipc/wiping_allocator.hpp"

namespace reboot::ipc {

using ClientMessage =
    std::variant<contracts::ipc::Hello, contracts::ipc::Call, contracts::ipc::Start, contracts::ipc::Cancel,
                 contracts::ipc::Attach, contracts::ipc::Release, contracts::ipc::Subscribe,
                 contracts::ipc::Unsubscribe, contracts::ipc::Credit, contracts::ipc::SecretPut,
                 contracts::ipc::SecretReveal, contracts::ipc::LogWrite, contracts::common::Ping,
                 contracts::ipc::Goodbye>;

using EngineMessage =
    std::variant<contracts::ipc::HelloAck, contracts::ipc::Reply, contracts::ipc::Started, contracts::ipc::OpResult,
                 contracts::ipc::EventBatch, contracts::ipc::Resync, contracts::ipc::ForegroundHint,
                 contracts::common::Pong, contracts::ipc::Goodbye>;

// Covers no capability ids. One per stream and direction; not thread-safe.
class IpcCodec {
public:
    IpcCodec();
    ~IpcCodec();
    IpcCodec(const IpcCodec&) = delete;
    IpcCodec& operator=(const IpcCodec&) = delete;

    // contracts.malformed_frame (also over kIpcFrameCap) or contracts.unexpected_frame; the
    // caller then closes the stream.
    [[nodiscard]] Result<std::vector<ClientMessage>> feed_from_client(std::span<const u8> bytes);
    [[nodiscard]] Result<std::vector<EngineMessage>> feed_from_engine(std::span<const u8> bytes);

    [[nodiscard]] std::size_t buffered() const noexcept;

    [[nodiscard]] static Result<ClientMessage> decode_client(const RawFrame& frame);
    [[nodiscard]] static Result<EngineMessage> decode_engine(const RawFrame& frame);

    template <ContractMessage T>
    [[nodiscard]] static contracts::ipc::Bytes encode(const T& message) {
        return encode_contract_frame(message);
    }
    [[nodiscard]] static contracts::ipc::Bytes encode(const ClientMessage& message);
    [[nodiscard]] static contracts::ipc::Bytes encode(const EngineMessage& message);
    // False for a frame whose payload the receiving codec refuses as over kIpcFrameCap.
    [[nodiscard]] static bool fits_frame_cap(std::span<const u8> frame) noexcept;

    // Secret frames are written into a buffer sized up front, so encoding never reallocates.
    [[nodiscard]] static SecretBytes encode_secret_put(std::span<const u8> target, const SecretBytes& secret);
    [[nodiscard]] static SecretBytes encode_secret_reply(u64 req_id, const SecretBytes& secret);

private:
    template <class Message, class Decode>
    Result<std::vector<Message>> feed(std::span<const u8> bytes, Decode decode);

    Framer framer_;
    // Holds partial frames itself so the Framer never buffers a copy; consumed bytes are wiped.
    WipedBytes pending_;
};

}  // namespace reboot::ipc
