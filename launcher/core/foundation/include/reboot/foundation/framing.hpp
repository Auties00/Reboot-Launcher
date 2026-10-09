#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"
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
class Framer {
public:
    using Status = sb::wire::StreamFramer::Status;

    explicit Framer(std::size_t max_frame) : framer_(max_frame) {}

    // Calls on_frame(RawFrame) for each complete frame; a false return stops and reports malformed.
    template <class F>
    Status feed(std::span<const u8> data, F&& on_frame) {
        return framer_.feed(data, [&](const sb::wire::FrameView& frame) {
            return on_frame(RawFrame{static_cast<u64>(frame.type), frame.payload});
        });
    }

    [[nodiscard]] std::size_t buffered() const noexcept { return framer_.buffered(); }

private:
    sb::wire::StreamFramer framer_;
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
