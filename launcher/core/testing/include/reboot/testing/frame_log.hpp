#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::testing {

struct OwnedFrame {
    u64 type = 0;
    std::vector<u8> payload;
};

// Covers no capability ids (decision testing-strategy).
// Splits what a peer wrote into frames and keeps them, for asserting on a private protocol.
class FrameLog {
public:
    explicit FrameLog(std::size_t max_frame) : framer_(max_frame) {}

    // contracts.malformed_frame once the stream stops parsing; later input is then ignored.
    Result<void> feed(std::span<const u8> bytes);

    [[nodiscard]] const std::vector<OwnedFrame>& frames() const noexcept { return frames_; }
    [[nodiscard]] std::size_t count(u64 type) const;
    [[nodiscard]] bool malformed() const noexcept { return malformed_; }

    // Every frame of T in order, or the first one's decode error, so a malformed frame is never skipped.
    template <ContractMessage T>
    [[nodiscard]] Result<std::vector<T>> all() const {
        std::vector<T> out;
        for (const OwnedFrame& frame : frames_) {
            if (frame.type != contract_frame_type_v<T>) continue;
            Result<T> message = decode_contract<T>(frame.payload);
            if (!message) return std::unexpected(std::move(message.error()));
            out.push_back(std::move(*message));
        }
        return out;
    }

    // The newest frame of T decoded, never an older one; nullopt when none arrived.
    template <ContractMessage T>
    [[nodiscard]] std::optional<Result<T>> last() const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it)
            if (it->type == contract_frame_type_v<T>) return decode_contract<T>(it->payload);
        return std::nullopt;
    }

    void clear() noexcept { frames_.clear(); }

private:
    Framer framer_;
    std::vector<OwnedFrame> frames_;
    bool malformed_ = false;
};

// Decodes `frame` as whichever of `Messages` owns its type; false when none does or decoding fails.
template <ContractMessage... Messages>
[[nodiscard]] bool decode_any_of(const RawFrame& frame) {
    return ((is_frame<Messages>(frame) && decode_contract<Messages>(frame.payload).has_value()) || ...);
}

}  // namespace rb::testing
