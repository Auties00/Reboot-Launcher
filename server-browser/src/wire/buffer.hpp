#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/features.hpp"
#include "core/types.hpp"

namespace sb::wire {

// Growable output buffer. Encoders write varints in place, so no intermediate copies.
// The vector is storage only (its size is the capacity); n_ is the written length, so every write
// is one inline bounds check instead of a vector::insert call.
class Writer {
public:
    Writer() = default;
    explicit Writer(std::size_t reserve) { buf_.resize(reserve); }
    // Writes into a recycled buffer, keeping its allocation.
    explicit Writer(std::vector<u8>&& storage, std::size_t reserve) : buf_(std::move(storage)) {
        if (buf_.size() < reserve) buf_.resize(reserve);
    }

    void put(u8 b) {
        ensure(1);
        buf_[n_++] = b;
    }
    void put(const void* p, std::size_t n) {
        ensure(n);
        if (n) std::memcpy(buf_.data() + n_, p, n);
        n_ += n;
    }
    void put(std::string_view s) { put(s.data(), s.size()); }
    void put(std::span<const u8> s) { put(s.data(), s.size()); }

    // Protobuf base-128 varint.
    void varint(u64 v) {
        ensure(10);
        u8* p = buf_.data() + n_;
        while (v >= 0x80) {
            *p++ = static_cast<u8>(v | 0x80);
            v >>= 7;
        }
        *p++ = static_cast<u8>(v);
        n_ = static_cast<std::size_t>(p - buf_.data());
    }

    // QUIC variable-length integer (RFC 9000 §16), used only for framing.
    void quic_varint(u64 v) {
        ensure(8);
        u8* p = buf_.data() + n_;
        if (v < (1u << 6)) {
            p[0] = static_cast<u8>(v);
            n_ += 1;
        } else if (v < (1u << 14)) {
            p[0] = static_cast<u8>(0x40 | (v >> 8));
            p[1] = static_cast<u8>(v);
            n_ += 2;
        } else if (v < (1u << 30)) {
            p[0] = static_cast<u8>(0x80 | (v >> 24));
            p[1] = static_cast<u8>(v >> 16);
            p[2] = static_cast<u8>(v >> 8);
            p[3] = static_cast<u8>(v);
            n_ += 4;
        } else {
            SB_ASSERT(v < (u64{1} << 62));
            for (int i = 7; i >= 0; --i) {
                p[i] = static_cast<u8>(v);
                v >>= 8;
            }
            p[0] |= 0xC0;
            n_ += 8;
        }
    }

    // Skips n bytes to be written later through data(); returns their offset.
    std::size_t skip(std::size_t n) {
        ensure(n);
        const std::size_t at = n_;
        n_ += n;
        return at;
    }

    // Opens an n-byte gap at offset `at`, shifting what follows.
    void insert_gap(std::size_t at, std::size_t n) {
        SB_ASSERT(at <= n_);
        ensure(n);
        std::memmove(buf_.data() + at + n, buf_.data() + at, n_ - at);
        n_ += n;
    }

    // Reserves room for a 4-byte QUIC varint length, returns its offset for patch_len4().
    std::size_t reserve_len4() { return skip(4); }

    void patch_len4(std::size_t at) {
        const std::size_t len = n_ - at - 4;
        SB_ASSERT(len < (1u << 30));
        buf_[at] = static_cast<u8>(0x80 | (len >> 24));
        buf_[at + 1] = static_cast<u8>(len >> 16);
        buf_[at + 2] = static_cast<u8>(len >> 8);
        buf_[at + 3] = static_cast<u8>(len);
    }

    [[nodiscard]] std::size_t size() const noexcept { return n_; }
    [[nodiscard]] const u8* data() const noexcept { return buf_.data(); }
    [[nodiscard]] u8* data() noexcept { return buf_.data(); }
    [[nodiscard]] std::span<const u8> view() const noexcept { return {buf_.data(), n_}; }
    void clear() noexcept { n_ = 0; }
    void truncate(std::size_t n) noexcept {
        SB_ASSERT(n <= n_);
        n_ = n;
    }
    std::vector<u8> take() noexcept {
        buf_.resize(n_);
        n_ = 0;
        return std::move(buf_);
    }

private:
    void ensure(std::size_t n) {
        if (buf_.size() - n_ < n) [[unlikely]] grow(n);
    }
    SB_NOINLINE void grow(std::size_t n) { buf_.resize(std::max({buf_.size() * 2, n_ + n, std::size_t{64}})); }

    std::vector<u8> buf_;
    std::size_t n_ = 0;
};

[[nodiscard]] constexpr std::size_t varint_size(u64 v) noexcept {
    std::size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

[[nodiscard]] constexpr std::size_t quic_varint_size(u64 v) noexcept {
    return v < (1u << 6) ? 1 : v < (1u << 14) ? 2 : v < (1u << 30) ? 4 : 8;
}

[[nodiscard]] constexpr u64 zigzag(i64 v) noexcept {
    return (static_cast<u64>(v) << 1) ^ static_cast<u64>(v >> 63);
}
[[nodiscard]] constexpr i64 unzigzag(u64 v) noexcept {
    return static_cast<i64>(v >> 1) ^ -static_cast<i64>(v & 1);
}

// Bounds-checked reader. Any malformed input sets the sticky error flag; callers check ok() once.
class Reader {
public:
    Reader() = default;
    explicit Reader(std::span<const u8> s) noexcept : p_(s.data()), end_(s.data() + s.size()) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool empty() const noexcept { return p_ >= end_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return static_cast<std::size_t>(end_ - p_); }
    [[nodiscard]] const u8* pos() const noexcept { return p_; }
    void fail() noexcept {
        ok_ = false;
        p_ = end_;
    }

    u64 varint() noexcept {
        u64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p_ >= end_) {
                fail();
                return 0;
            }
            const u8 b = *p_++;
            v |= u64{b & 0x7Fu} << shift;
            if (!(b & 0x80)) {
                if (shift == 63 && b > 1) fail();  // overflow past 64 bits
                return v;
            }
        }
        fail();
        return 0;
    }

    u64 quic_varint() noexcept {
        if (p_ >= end_) {
            fail();
            return 0;
        }
        const std::size_t len = std::size_t{1} << (*p_ >> 6);
        if (remaining() < len) {
            fail();
            return 0;
        }
        u64 v = *p_++ & 0x3Fu;
        for (std::size_t i = 1; i < len; ++i) v = (v << 8) | *p_++;
        return v;
    }

    std::span<const u8> bytes(std::size_t n) noexcept {
        if (remaining() < n) {
            fail();
            return {};
        }
        std::span<const u8> s(p_, n);
        p_ += n;
        return s;
    }

    u8 byte() noexcept {
        if (p_ >= end_) {
            fail();
            return 0;
        }
        return *p_++;
    }

private:
    const u8* p_ = nullptr;
    const u8* end_ = nullptr;
    bool ok_ = true;
};

}  // namespace sb::wire
