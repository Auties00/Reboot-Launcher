#include "reboot/ipc/ipc_codec.hpp"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include "wire/buffer.hpp"

namespace reboot::ipc {

namespace {

namespace wire = contracts::ipc;

constexpr u8 kLenWireType = 2;
constexpr u8 kVarintWireType = 0;

template <class Variant, std::size_t I = 0>
Result<Variant> decode_alternative(const RawFrame& frame, std::string_view direction) {
    if constexpr (I == std::variant_size_v<Variant>) {
        return make_diag(ErrorDomain::Contracts, kUnexpectedFrame)
            .arg("expected", direction)
            .arg("actual", frame.type)
            .fail();
    } else {
        using T = std::variant_alternative_t<I, Variant>;
        if (!is_frame<T>(frame)) return decode_alternative<Variant, I + 1>(frame, direction);
        Result<T> message = decode_contract<T>(frame.payload);
        if (!message) return std::unexpected(std::move(message.error()));
        return Variant{std::in_place_index<I>, std::move(*message)};
    }
}

[[nodiscard]] Diagnostic malformed(u64 frame_type) {
    return make_diag(ErrorDomain::Contracts, kMalformedFrame).arg("frame_type", frame_type).build();
}

// Writes into a vector whose capacity was sized up front, so nothing reallocates.
class ExactWriter {
public:
    explicit ExactWriter(std::size_t size) { bytes_.reserve(size); }

    void byte(u8 value) { bytes_.push_back(value); }
    void varint(u64 value) {
        while (value >= 0x80) {
            bytes_.push_back(static_cast<u8>(value | 0x80));
            value >>= 7;
        }
        bytes_.push_back(static_cast<u8>(value));
    }
    void quic_varint(u64 value) {
        const std::size_t size = sb::wire::quic_varint_size(value);
        const u8 prefix = size == 1 ? 0x00 : size == 2 ? 0x40 : size == 4 ? 0x80 : 0xC0;
        for (std::size_t i = size; i-- > 0;) {
            const auto shifted = static_cast<u8>(value >> (8 * i));
            bytes_.push_back(i + 1 == size ? static_cast<u8>(shifted | prefix) : shifted);
        }
    }
    // The fixed four-byte length encode_contract_frame writes.
    void length4(std::size_t length) {
        byte(static_cast<u8>(0x80 | (length >> 24)));
        byte(static_cast<u8>(length >> 16));
        byte(static_cast<u8>(length >> 8));
        byte(static_cast<u8>(length));
    }
    void field_bytes(u64 field, std::span<const u8> value) {
        varint(field << 3 | kLenWireType);
        varint(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    [[nodiscard]] std::vector<u8> take() { return std::move(bytes_); }

private:
    std::vector<u8> bytes_;
};

[[nodiscard]] std::size_t field_bytes_size(u64 field, std::size_t length) {
    return sb::wire::varint_size(field << 3 | kLenWireType) + sb::wire::varint_size(length) + length;
}

[[nodiscard]] std::size_t frame_size(u64 type, std::size_t payload) {
    return sb::wire::quic_varint_size(type) + 4 + payload;
}

// Decoded messages that are dropped may still hold a secret.
void wipe_dropped(std::vector<ClientMessage>& messages) {
    for (ClientMessage& message : messages)
        if (auto* put = std::get_if<wire::SecretPut>(&message)) secure_wipe(put->bytes.data(), put->bytes.size());
}
void wipe_dropped(std::vector<EngineMessage>&) {}

}  // namespace

IpcCodec::IpcCodec() : framer_(kIpcFrameCap) {}

IpcCodec::~IpcCodec() { secure_wipe(pending_.data(), pending_.size()); }

template <class Message, class Decode>
Result<std::vector<Message>> IpcCodec::feed(std::span<const u8> bytes, Decode decode) {
    std::span<const u8> in = bytes;
    const bool held = !pending_.empty();
    if (held) {
        pending_.insert(pending_.end(), bytes.begin(), bytes.end());
        in = pending_;
    }

    // Only whole frames reach the Framer, so it never keeps a copy of a partial one.
    std::size_t complete = 0;
    std::optional<Diagnostic> error;
    for (;;) {
        sb::wire::Reader reader(in.subspan(complete));
        const u64 type = reader.quic_varint();
        const u64 length = reader.quic_varint();
        if (!reader.ok()) break;
        if (length > kIpcFrameCap) {
            error = malformed(type);
            break;
        }
        if (reader.remaining() < length) break;
        complete = static_cast<std::size_t>(reader.pos() - in.data()) + static_cast<std::size_t>(length);
    }

    std::vector<Message> messages;
    if (!error && complete > 0) {
        const Framer::Status status = framer_.feed(in.first(complete), [&](const RawFrame& frame) {
            Result<Message> message = decode(frame);
            if (!message) {
                error = std::move(message.error());
                return false;
            }
            messages.push_back(std::move(*message));
            return true;
        });
        if (!error && status != Framer::Status::ok) error = malformed(0);
    }

    if (error) {
        wipe_dropped(messages);
        secure_wipe(pending_.data(), pending_.size());
        pending_.clear();
        return std::unexpected(std::move(*error));
    }
    if (!held) {
        pending_.assign(in.begin() + static_cast<std::ptrdiff_t>(complete), in.end());
    } else if (complete > 0) {
        const std::size_t remain = pending_.size() - complete;
        std::copy(pending_.begin() + static_cast<std::ptrdiff_t>(complete), pending_.end(), pending_.begin());
        secure_wipe(pending_.data() + remain, complete);
        pending_.resize(remain);
    }
    return messages;
}

Result<std::vector<ClientMessage>> IpcCodec::feed_from_client(std::span<const u8> bytes) {
    return feed<ClientMessage>(bytes, [](const RawFrame& frame) { return decode_client(frame); });
}

Result<std::vector<EngineMessage>> IpcCodec::feed_from_engine(std::span<const u8> bytes) {
    return feed<EngineMessage>(bytes, [](const RawFrame& frame) { return decode_engine(frame); });
}

std::size_t IpcCodec::buffered() const noexcept { return pending_.size(); }

Result<ClientMessage> IpcCodec::decode_client(const RawFrame& frame) {
    return decode_alternative<ClientMessage>(frame, "ipc.client");
}

Result<EngineMessage> IpcCodec::decode_engine(const RawFrame& frame) {
    return decode_alternative<EngineMessage>(frame, "ipc.engine");
}

wire::Bytes IpcCodec::encode(const ClientMessage& message) {
    return std::visit([](const auto& alternative) { return encode_contract_frame(alternative); }, message);
}

wire::Bytes IpcCodec::encode(const EngineMessage& message) {
    return std::visit([](const auto& alternative) { return encode_contract_frame(alternative); }, message);
}

bool IpcCodec::fits_frame_cap(std::span<const u8> frame) noexcept {
    sb::wire::Reader reader(frame);
    static_cast<void>(reader.quic_varint());
    const u64 length = reader.quic_varint();
    return reader.ok() && length <= kIpcFrameCap;
}

// Byte-identical to encode(SecretPut{target, secret}), without a copy of the secret in a message.
SecretBytes IpcCodec::encode_secret_put(std::span<const u8> target, const SecretBytes& secret) {
    const std::span<const u8> bytes = secret.reveal();
    const std::size_t payload = (target.empty() ? 0 : field_bytes_size(1, target.size())) +
                                (bytes.empty() ? 0 : field_bytes_size(2, bytes.size()));
    constexpr u64 kType = contract_frame_type_v<wire::SecretPut>;
    ExactWriter out(frame_size(kType, payload));
    out.quic_varint(kType);
    out.length4(payload);
    if (!target.empty()) out.field_bytes(1, target);
    if (!bytes.empty()) out.field_bytes(2, bytes);
    return SecretBytes{out.take()};
}

// Byte-identical to encode(Reply{req_id, secret bytes, nullopt}).
SecretBytes IpcCodec::encode_secret_reply(u64 req_id, const SecretBytes& secret) {
    const std::span<const u8> bytes = secret.reveal();
    const std::size_t id_size = req_id == 0 ? 0 : 1 + sb::wire::varint_size(req_id);
    const std::size_t payload = id_size + field_bytes_size(2, bytes.size());
    constexpr u64 kType = contract_frame_type_v<wire::Reply>;
    ExactWriter out(frame_size(kType, payload));
    out.quic_varint(kType);
    out.length4(payload);
    if (req_id != 0) {
        out.byte(u8{1} << 3 | kVarintWireType);
        out.varint(req_id);
    }
    out.field_bytes(2, bytes);
    return SecretBytes{out.take()};
}

}  // namespace reboot::ipc
