#pragma once

#include <span>
#include <vector>

#include "core/features.hpp"
#include "wire/buffer.hpp"
#include "wire/codec.hpp"
#include "wire/messages.hpp"

namespace sb::wire {

// frame := quic_varint type | quic_varint length | payload
// Lengths are written with a fixed width (2 bytes for datagram-sized frames, 4 otherwise);
// RFC 9000 varints allow non-minimal widths, and fixed widths let us patch in place.
enum class LenWidth : u8 { two = 2, four = 4 };

template <class T>
void encode_frame(Writer& w, const T& msg, LenWidth width = LenWidth::four) {
    w.quic_varint(static_cast<u64>(frame_type_v<T>));
    const std::size_t at = w.size();
    w.raw().resize(at + static_cast<std::size_t>(width));
    encode(w, msg);
    const std::size_t len = w.size() - at - static_cast<std::size_t>(width);
    u8* p = w.data() + at;
    if (width == LenWidth::two) {
        SB_ASSERT(len < (1u << 14));
        p[0] = static_cast<u8>(0x40 | (len >> 8));
        p[1] = static_cast<u8>(len);
    } else {
        SB_ASSERT(len < (1u << 30));
        p[0] = static_cast<u8>(0x80 | (len >> 24));
        p[1] = static_cast<u8>(len >> 16);
        p[2] = static_cast<u8>(len >> 8);
        p[3] = static_cast<u8>(len);
    }
}

template <class T>
[[nodiscard]] Bytes frame_bytes(const T& msg, LenWidth width = LenWidth::four) {
    Writer w;
    encode_frame(w, msg, width);
    return w.take();
}

struct FrameView {
    FrameType type{};
    std::span<const u8> payload;
};

// Splits a complete buffer (a datagram or a fully received stream) into frames.
// Returns false on a truncated or oversized frame.
template <class F>
bool for_each_frame(std::span<const u8> buf, std::size_t max_frame, F&& f) {
    Reader r(buf);
    while (r.ok() && !r.empty()) {
        const u64 type = r.quic_varint();
        const u64 len = r.quic_varint();
        if (!r.ok() || len > max_frame) return false;
        auto payload = r.bytes(len);
        if (!r.ok()) return false;
        if (!f(FrameView{static_cast<FrameType>(type), payload})) return false;
    }
    return r.ok();
}

// Incremental parser for a byte stream; buffers partial frames across receive events.
class StreamFramer {
public:
    explicit StreamFramer(std::size_t max_frame) : max_frame_(max_frame) {}

    enum class Status { ok, too_large, malformed };

    // Appends `data` and invokes f(FrameView) for each complete frame. Stops on the first
    // frame f rejects (returns false) and reports it as malformed.
    template <class F>
    Status feed(std::span<const u8> data, F&& f) {
        std::span<const u8> in = data;
        if (!pending_.empty()) {
            pending_.insert(pending_.end(), data.begin(), data.end());
            in = pending_;
        }
        std::size_t consumed = 0;
        Status st = Status::ok;
        for (;;) {
            Reader r(in.subspan(consumed));
            const u64 type = r.quic_varint();
            const u64 len = r.quic_varint();
            if (!r.ok()) break;  // header incomplete
            if (len > max_frame_) {
                st = Status::too_large;
                break;
            }
            const std::size_t header = static_cast<std::size_t>(r.pos() - (in.data() + consumed));
            if (r.remaining() < len) break;  // body incomplete
            const FrameView fv{static_cast<FrameType>(type), in.subspan(consumed + header, len)};
            consumed += header + len;
            if (!f(fv)) {
                st = Status::malformed;
                break;
            }
        }
        if (st != Status::ok) {
            pending_.clear();
            return st;
        }
        if (pending_.empty()) {
            pending_.assign(in.begin() + static_cast<std::ptrdiff_t>(consumed), in.end());
        } else {
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
        if (pending_.size() > max_frame_ + 16) return Status::too_large;
        return Status::ok;
    }

    [[nodiscard]] std::size_t buffered() const noexcept { return pending_.size(); }

private:
    std::size_t max_frame_;
    std::vector<u8> pending_;
};

template <class T>
[[nodiscard]] bool decode_frame(const FrameView& f, T& out) {
    return f.type == frame_type_v<T> && decode(f.payload, out);
}

}  // namespace sb::wire
