#include "reboot/foundation/sha256.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>

namespace reboot {

namespace {

constexpr std::array<u32, 64> kRound{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

}  // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::compress(const u8* block) noexcept {
    std::array<u32, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
        w[i] = u32{block[4 * i]} << 24 | u32{block[4 * i + 1]} << 16 | u32{block[4 * i + 2]} << 8 | u32{block[4 * i + 3]};
    for (std::size_t i = 16; i < 64; ++i) {
        const u32 s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const u32 s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    u32 e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const u32 s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const u32 choose = (e & f) ^ (~e & g);
        const u32 t1 = h + s1 + choose + kRound[i] + w[i];
        const u32 s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const u32 majority = (a & b) ^ (a & c) ^ (b & c);
        const u32 t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const u8> data) noexcept {
    total_bytes_ += data.size();
    if (block_used_ != 0) {
        const std::size_t take = std::min(data.size(), block_.size() - block_used_);
        std::copy_n(data.begin(), take, block_.begin() + static_cast<std::ptrdiff_t>(block_used_));
        block_used_ += take;
        data = data.subspan(take);
        if (block_used_ < block_.size()) return;
        compress(block_.data());
        block_used_ = 0;
    }
    // Whole blocks are hashed in place, without the copy.
    for (; data.size() >= block_.size(); data = data.subspan(block_.size())) compress(data.data());
    std::ranges::copy(data, block_.begin());
    block_used_ = data.size();
}

std::array<u8, 32> Sha256::finish() noexcept {
    const u64 bits = total_bytes_ * 8;
    const u8 pad = 0x80;
    update({&pad, 1});
    const u8 zero = 0;
    while (block_used_ != 56) update({&zero, 1});
    std::array<u8, 8> length{};
    for (std::size_t i = 0; i < 8; ++i) length[i] = static_cast<u8>(bits >> (56 - 8 * i));
    update(length);

    std::array<u8, 32> digest{};
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t k = 0; k < 4; ++k) digest[4 * i + k] = static_cast<u8>(state_[i] >> (24 - 8 * k));
    return digest;
}

std::array<u8, 32> sha256(std::span<const u8> data) noexcept {
    Sha256 hash;
    hash.update(data);
    return hash.finish();
}

std::string to_hex(std::span<const u8> data) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (const u8 byte : data) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0xF]);
    }
    return out;
}

bool constant_time_equal(std::span<const u8> a, std::span<const u8> b) noexcept {
    if (a.size() != b.size()) return false;
    u8 diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<u8>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

}  // namespace reboot
