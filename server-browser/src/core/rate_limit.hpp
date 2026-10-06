#pragma once

#include <algorithm>
#include <atomic>
#include <memory>

#include "core/features.hpp"
#include "core/hash.hpp"
#include "core/types.hpp"

namespace sb {

struct RateSpec {
    u64 per_second_micro = 1'000'000;  // refill rate in millionths of a token per second
    u32 burst = 1;

    static constexpr RateSpec per_second(double rate, u32 burst) {
        return {static_cast<u64>(rate * 1e6 + 0.5), burst};
    }
    static constexpr RateSpec per_minute(double rate, u32 burst) {
        return {static_cast<u64>(rate * 1e6 / 60.0 + 0.5), burst};
    }
};

// Single-owner token bucket. Tokens are kept in nano-units so that per_second_micro is exactly
// the refill per millisecond and no fractional refill is ever lost.
class TokenBucket {
public:
    explicit TokenBucket(RateSpec spec = {}) noexcept : spec_(spec), tokens_(u64{spec.burst} * kUnit) {}

    bool take(u64 now_ms, u32 cost = 1) noexcept {
        refill(now_ms);
        const u64 need = u64{cost} * kUnit;
        if (tokens_ < need) return false;
        tokens_ -= need;
        return true;
    }

    // Milliseconds until `cost` tokens are available.
    [[nodiscard]] u64 wait_ms(u64 now_ms, u32 cost = 1) noexcept {
        refill(now_ms);
        const u64 need = u64{cost} * kUnit;
        if (tokens_ >= need) return 0;
        if (spec_.per_second_micro == 0) return ~u64{0};
        return (need - tokens_ + spec_.per_second_micro - 1) / spec_.per_second_micro;
    }

private:
    static constexpr u64 kUnit = 1'000'000'000;

    void refill(u64 now_ms) noexcept {
        if (last_ms_ == 0) last_ms_ = now_ms;
        const u64 elapsed = now_ms > last_ms_ ? now_ms - last_ms_ : 0;
        last_ms_ = now_ms;
        tokens_ = std::min(u64{spec_.burst} * kUnit, tokens_ + elapsed * spec_.per_second_micro);
    }

    RateSpec spec_;
    u64 tokens_;
    u64 last_ms_ = 0;
};

// Lock-free, fixed-memory rate table shared by all threads (used on the accept path).
// Keys hash into buckets; collisions only make limits stricter, never looser for the caller.
// Each cell packs 16-bit tokens (x16 fixed point) with a 48-bit millisecond timestamp.
class AtomicRateTable {
public:
    AtomicRateTable(std::size_t cells_pow2, RateSpec spec)
        : mask_(cells_pow2 - 1), spec_(spec), cells_(std::make_unique<std::atomic<u64>[]>(cells_pow2)) {
        SB_ASSERT((cells_pow2 & mask_) == 0);
        SB_ASSERT(spec.burst * kScale < 0xFFFF);
    }

    bool take(u64 key_hash, u64 now_ms) noexcept {
        auto& cell = cells_[mix64(key_hash) & mask_];
        u64 cur = cell.load(std::memory_order_relaxed);
        const u64 cap = u64{spec_.burst} * kScale;
        const u64 now = now_ms & kTsMask;
        const u64 rate = spec_.per_second_micro * kScale;  // fixed-point tokens per 1e9 ms
        for (;;) {
            u64 tokens = cur >> 48;
            u64 ts = cur & kTsMask;
            if (cur == 0) {  // never-used cell starts full
                tokens = cap;
                ts = now;
            }
            const u64 elapsed = now >= ts ? now - ts : 0;
            const u64 added = rate ? elapsed * rate / 1'000'000'000 : 0;
            // Advance the timestamp only by the time actually converted into tokens, so
            // frequent calls never drop fractional refill.
            if (tokens + added >= cap) {
                tokens = cap;
                ts = now;
            } else if (added > 0) {
                tokens += added;
                ts += added * 1'000'000'000 / rate;
            }
            if (tokens < kScale) return false;
            const u64 next = ((tokens - kScale) << 48) | ts;
            if (cell.compare_exchange_weak(cur, next, std::memory_order_relaxed)) return true;
        }
    }

private:
    static constexpr u64 kScale = 16;
    static constexpr u64 kTsMask = (u64{1} << 48) - 1;
    const std::size_t mask_;
    const RateSpec spec_;
    std::unique_ptr<std::atomic<u64>[]> cells_;
};

}  // namespace sb
