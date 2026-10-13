#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/base/secret.hpp"
#include "reboot/base/types.hpp"
#include "wire/codec.hpp"
#include "wire/frame.hpp"

namespace rb {

inline constexpr std::size_t kIpcFrameCap = std::size_t{16} << 20;
inline constexpr std::size_t kChildFrameCap = std::size_t{4} << 20;
inline constexpr std::size_t kGameControlFrameCap = std::size_t{1} << 20;

inline constexpr std::chrono::seconds kLivenessPingInterval{5};
inline constexpr int kLivenessMissLimit = 3;

inline constexpr MessageId kMalformedFrame{"contracts.malformed_frame"};
inline constexpr MessageId kUnexpectedFrame{"contracts.unexpected_frame"};

struct RawFrame {
    u64 type = 0;
    std::span<const u8> payload;
};

// Every private channel (engine IPC, child stdio, game control) frames with the sb layout:
// quic varint type | quic varint length | payload.
// A partial frame may hold a token or a SecretPut, so every byte the buffer lets go of is wiped.
class Framer {
public:
    using Status = sb::wire::StreamFramer::Status;

    explicit Framer(std::size_t max_frame) : max_frame_(max_frame) {}
    Framer(Framer&&) noexcept = default;
    Framer& operator=(Framer&&) noexcept = default;
    Framer(const Framer&) = delete;
    Framer& operator=(const Framer&) = delete;

    // Calls on_frame(RawFrame) for each complete frame; a false return stops and reports malformed.
    // After any status but ok the buffered bytes are dropped.
    template <class F>
    Status feed(std::span<const u8> data, F&& on_frame) {
        const bool held = !pending_.empty();
        if (held) pending_.insert(pending_.end(), data.begin(), data.end());
        const std::span<const u8> in = held ? std::span<const u8>(pending_) : data;
        std::size_t consumed = 0;
        Status status = Status::ok;
        for (;;) {
            sb::wire::Reader reader(in.subspan(consumed));
            const u64 type = reader.quic_varint();
            const u64 length = reader.quic_varint();
            if (!reader.ok()) break;
            if (length > max_frame_) {
                status = Status::too_large;
                break;
            }
            if (reader.remaining() < length) break;
            const auto header = static_cast<std::size_t>(reader.pos() - (in.data() + consumed));
            const RawFrame frame{type, in.subspan(consumed + header, static_cast<std::size_t>(length))};
            consumed += header + static_cast<std::size_t>(length);
            if (!on_frame(frame)) {
                status = Status::malformed;
                break;
            }
        }
        if (status == Status::ok) {
            keep_tail(in, consumed, held);
            if (pending_.size() <= max_frame_ + kMaxHeader) return Status::ok;
            status = Status::too_large;
        }
        drop();
        return status;
    }

    [[nodiscard]] std::size_t buffered() const noexcept { return pending_.size(); }

private:
    // Lets the tests read the spare capacity.
    friend struct FramerProbe;

    // Two quic varints of at most 8 bytes each.
    static constexpr std::size_t kMaxHeader = 16;

    void keep_tail(std::span<const u8> in, std::size_t consumed, bool held) {
        if (!held) {
            pending_.assign(in.begin() + static_cast<std::ptrdiff_t>(consumed), in.end());
            return;
        }
        if (consumed == 0) return;
        const std::size_t remain = pending_.size() - consumed;
        std::copy(pending_.begin() + static_cast<std::ptrdiff_t>(consumed), pending_.end(), pending_.begin());
        secure_wipe(pending_.data() + remain, consumed);
        pending_.resize(remain);
    }

    void drop() noexcept {
        secure_wipe(pending_.data(), pending_.size());
        pending_.clear();
    }

    std::size_t max_frame_;
    WipedBytes pending_;
};

// Found by argument-dependent lookup on std::type_identity<T>, so REBOOT_CONTRACT_FRAME works
// from the message's own namespace.
template <class T>
concept ContractMessage = sb::wire::Message<T> && requires {
    { reboot_contract_frame_type(std::type_identity<T>{}) } -> std::same_as<u64>;
};

template <ContractMessage T>
inline constexpr u64 contract_frame_type_v = reboot_contract_frame_type(std::type_identity<T>{});

template <ContractMessage T>
[[nodiscard]] std::vector<u8> encode_contract_frame(const T& message) {
    sb::wire::Writer writer;
    writer.quic_varint(contract_frame_type_v<T>);
    const std::size_t length_at = writer.skip(4);
    sb::wire::encode(writer, message);
    const std::size_t length = writer.size() - length_at - 4;
    u8* p = writer.data() + length_at;
    p[0] = static_cast<u8>(0x80 | (length >> 24));
    p[1] = static_cast<u8>(length >> 16);
    p[2] = static_cast<u8>(length >> 8);
    p[3] = static_cast<u8>(length);
    return writer.take();
}

template <ContractMessage T>
[[nodiscard]] Result<T> decode_contract(std::span<const u8> payload) {
    T message{};
    if (!sb::wire::decode(payload, message))
        return make_diag(ErrorDomain::Contracts, kMalformedFrame).arg("frame_type", contract_frame_type_v<T>).fail();
    return message;
}

template <ContractMessage T>
[[nodiscard]] Result<T> decode_contract(const RawFrame& frame) {
    if (frame.type != contract_frame_type_v<T>)
        return make_diag(ErrorDomain::Contracts, kUnexpectedFrame)
            .arg("expected", contract_frame_type_v<T>)
            .arg("actual", frame.type)
            .fail();
    return decode_contract<T>(frame.payload);
}

template <ContractMessage T>
[[nodiscard]] constexpr bool is_frame(const RawFrame& frame) noexcept {
    return frame.type == contract_frame_type_v<T>;
}

// Every game-control connection starts with "RBCTL\0" and the little-endian payload_abi.
inline constexpr std::array<u8, 6> kGameControlMagic{'R', 'B', 'C', 'T', 'L', 0};
inline constexpr std::size_t kGameControlPreambleSize = kGameControlMagic.size() + 2;

[[nodiscard]] constexpr std::array<u8, kGameControlPreambleSize> game_control_preamble(u16 payload_abi) noexcept {
    std::array<u8, kGameControlPreambleSize> out{};
    for (std::size_t i = 0; i < kGameControlMagic.size(); ++i) out[i] = kGameControlMagic[i];
    out[6] = static_cast<u8>(payload_abi);
    out[7] = static_cast<u8>(payload_abi >> 8);
    return out;
}

// The payload_abi, or nullopt when the magic does not match.
[[nodiscard]] constexpr std::optional<u16> parse_game_control_preamble(
    std::span<const u8, kGameControlPreambleSize> bytes) noexcept {
    for (std::size_t i = 0; i < kGameControlMagic.size(); ++i)
        if (bytes[i] != kGameControlMagic[i]) return std::nullopt;
    return static_cast<u16>(bytes[6] | (bytes[7] << 8));
}

}  // namespace rb

// Gives a contract message its frame type; used right after the struct, in its namespace.
#define REBOOT_CONTRACT_FRAME(Type, number)                                                      \
    [[nodiscard]] constexpr ::rb::u64 reboot_contract_frame_type(std::type_identity<Type>) noexcept { \
        return number;                                                                           \
    }
