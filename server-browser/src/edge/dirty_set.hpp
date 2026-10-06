#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

#include "core/flat_map.hpp"
#include "core/types.hpp"

namespace sb::edge {

// Per-connection latest-value-wins set of (view, entry) pairs whose current state still has to
// reach the client. Bounded by the subscribed windows: past 2x window, removals collapse into a
// WindowSync.
// A busy connection usually has only a few pairs pending, so the first kInline live in the Conn
// itself (no cache miss into a separate table); past that everything moves to a hash map until
// the set empties again.
class DirtySet {
public:
    static constexpr std::size_t kInline = 6;

    [[nodiscard]] static constexpr u64 key(u32 view_id, u32 handle) noexcept { return (u64{view_id} << 32) | handle; }

    // Returns true if the pair was not dirty before.
    bool mark(u32 view_id, u32 handle, u8 mask) {
        const u64 k = key(view_id, handle);
        if (!spilled_) {
            for (u8 i = 0; i < n_; ++i)
                if (keys_[i] == k) {
                    masks_[i] |= mask;
                    return false;
                }
            if (n_ < kInline) {
                keys_[n_] = k;
                masks_[n_++] = mask;
                return true;
            }
            spill();
        }
        auto [it, inserted] = map_.try_emplace(k, mask);
        if (!inserted) it->second |= mask;
        return inserted;
    }
    void erase_view(u32 view_id) {
        retain_view(view_id, [](u32) { return false; });
    }
    template <class Keep>
    void retain_view(u32 view_id, Keep&& keep) {
        if (!spilled_) {
            u8 w = 0;
            for (u8 i = 0; i < n_; ++i) {
                if (static_cast<u32>(keys_[i] >> 32) == view_id && !keep(static_cast<u32>(keys_[i]))) continue;
                keys_[w] = keys_[i];
                masks_[w++] = masks_[i];
            }
            n_ = w;
            return;
        }
        for (auto it = map_.begin(); it != map_.end();) {
            if (static_cast<u32>(it->first >> 32) == view_id && !keep(static_cast<u32>(it->first))) it = erase_it(it);
            else ++it;
        }
        if (map_.empty()) spilled_ = false;
    }
    [[nodiscard]] std::size_t count_view(u32 view_id) const {
        std::size_t n = 0;
        if (!spilled_) {
            for (u8 i = 0; i < n_; ++i) n += static_cast<u32>(keys_[i] >> 32) == view_id;
            return n;
        }
        for (const auto& [k, m] : map_) n += static_cast<u32>(k >> 32) == view_id;
        return n;
    }
    [[nodiscard]] bool empty() const noexcept { return n_ == 0 && !spilled_; }
    [[nodiscard]] std::size_t size() const noexcept { return spilled_ ? map_.size() : n_; }

    // Moves every pair out, sorted by view so a flush can build one Delta per view.
    void take_sorted(std::vector<std::pair<u64, u8>>& out) {
        out.clear();
        if (spilled_) {
            out.assign(map_.begin(), map_.end());
            map_.clear();
            spilled_ = false;
        } else {
            for (u8 i = 0; i < n_; ++i) out.emplace_back(keys_[i], masks_[i]);
            n_ = 0;
        }
        std::sort(out.begin(), out.end());
    }

private:
    void spill() {
        for (u8 i = 0; i < n_; ++i) map_.emplace(keys_[i], masks_[i]);
        n_ = 0;
        spilled_ = true;
    }
    template <class It>
    It erase_it(It it) {
        auto next = std::next(it);
        map_.erase(it);
        return next;
    }

    // Invariant: spilled_ means every pair is in map_ (and n_ == 0); otherwise map_ is empty.
    u8 n_ = 0;
    bool spilled_ = false;
    u8 masks_[kInline]{};
    u64 keys_[kInline]{};
    FlatMap<u64, u8> map_;
};

}  // namespace sb::edge
