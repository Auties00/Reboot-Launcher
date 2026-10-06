#pragma once

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/features.hpp"
#include "core/inplace_vector.hpp"
#include "core/types.hpp"
#include "security/crypto.hpp"
#include "wire/messages.hpp"

namespace sb::registry {

using security::Digest;

inline constexpr u32 kNoHandle = ~u32{0};
inline constexpr u16 kNoShard = ~u16{0};

// Visibility: an entry appears in views and queries only when all of these hold.
[[nodiscard]] constexpr bool visible_flags(u32 flags) noexcept {
    return (flags & wire::entry_flag::online) && (flags & wire::entry_flag::reachable) &&
           !(flags & wire::entry_flag::hidden);
}

// Immutable snapshot of an entry, published by the replica for shard and search threads.
// Replaced (never mutated) on every change; old versions are reclaimed once every ring
// consumer has moved past the event that retired them.
struct PubEntry {
    u32 handle = kNoHandle;
    Uuid id;
    std::string name;
    std::string author;
    std::string version;
    u32 bucket = 0;
    u32 players = 0;
    u32 max_players = 0;
    u32 flags = 0;
    wire::Region region{};
    u64 created_ms = 0;
    std::vector<u32> views;  // sorted ids of view windows this entry is currently inside

    [[nodiscard]] bool in_view(u32 view_id) const noexcept {
        return std::binary_search(views.begin(), views.end(), view_id);
    }

    void to_list_entry(wire::ListEntry& e) const {
        e.handle = handle;
        e.id = id;
        e.name = name;
        e.author = author;
        e.version = version;
        e.bucket = bucket;
        e.players = players;
        e.max_players = max_players;
        e.flags = flags;
        e.region = region;
        e.created_ms = created_ms;
    }
};

// Sort keys: `primary` carries the sort order, ties fall back to the folded name (name sort)
// and finally to the handle, so every key is unique.
struct SortKey {
    u64 primary = 0;
    u32 handle = 0;
};

[[nodiscard]] constexpr u64 players_primary(u32 players, u64 created_ms) noexcept {
    return (u64{~players} << 32) | u64{~static_cast<u32>(created_ms / 1000)};
}
[[nodiscard]] constexpr u64 newest_primary(u64 created_ms) noexcept { return ~created_ms; }
[[nodiscard]] inline u64 name_primary(std::string_view folded) noexcept {
    u64 v = 0;
    for (std::size_t i = 0; i < 8; ++i) v = (v << 8) | (i < folded.size() ? static_cast<u8>(folded[i]) : 0);
    return v;
}

// Replica-owned mutable record. Only the replica thread touches it.
struct Record {
    Uuid id;
    u32 handle = kNoHandle;

    // public
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    u32 bucket = wire::kBucketOther;
    u32 players = 0;
    u32 max_players = 0;
    wire::Region region{};
    bool hidden = false;
    bool reachable = true;
    bool online = true;
    u64 created_ms = 0;
    u64 updated_ms = 0;

    // private
    IpAddr addr;
    u16 port = 0;
    std::optional<Digest> password_mac;
    Digest token_hash{};
    u64 owner_edge = 0;
    u64 ver = 0;         // (lease_epoch << 32) | owner_seq
    u64 stream_seq = 0;  // last backbone sequence for this subject, for compare-and-set

    // owner-local state (meaningful only on the owning edge)
    u16 shard = kNoShard;
    u64 conn = 0;
    u32 probe_failures = 0;

    // index state
    bool indexed = false;
    u64 partitions[8]{};
    u8 npartitions = 0;
    SortKey keys[3]{};
    std::string folded_name;  // the name the indexes were built with
    const PubEntry* pub = nullptr;
    std::vector<u32> views;  // current window memberships (sorted)

    [[nodiscard]] u32 flags() const noexcept {
        u32 f = 0;
        if (password_mac) f |= wire::entry_flag::has_password;
        if (reachable) f |= wire::entry_flag::reachable;
        if (online) f |= wire::entry_flag::online;
        if (hidden) f |= wire::entry_flag::hidden;
        return f;
    }
    [[nodiscard]] bool visible() const noexcept { return visible_flags(flags()); }
    [[nodiscard]] u32 lease_epoch() const noexcept { return static_cast<u32>(ver >> 32); }

    void to_list_entry(wire::ListEntry& e) const {
        e.handle = handle;
        e.id = id;
        e.name = name;
        e.author = author;
        e.version = version;
        e.bucket = bucket;
        e.players = players;
        e.max_players = max_players;
        e.flags = flags();
        e.region = region;
        e.created_ms = created_ms;
    }
};

// ---- partitions ------------------------------------------------------------------------------

// A partition is one (bucket | all, password filter, region | all) combination. Every visible
// entry lives in up to 8 partitions, each holding one ordered index per sort.
[[nodiscard]] constexpr u64 partition_key(u32 bucket, wire::PasswordFilter pwd, wire::Region region) noexcept {
    return (u64{bucket} << 16) | (u64{static_cast<u8>(pwd)} << 8) | u64{static_cast<u8>(region)};
}

[[nodiscard]] inline u8 partitions_of(u32 bucket, bool has_password, wire::Region region, u64 out[8]) noexcept {
    const u32 buckets[2] = {wire::kBucketAll, bucket};
    const wire::PasswordFilter pwds[2] = {wire::PasswordFilter::any,
                                          has_password ? wire::PasswordFilter::only : wire::PasswordFilter::none};
    const wire::Region regions[2] = {wire::Region::all, region};
    const int nregions = region == wire::Region::all ? 1 : 2;
    u8 n = 0;
    for (u32 b : buckets)
        for (auto p : pwds)
            for (int r = 0; r < nregions; ++r) out[n++] = partition_key(b, p, regions[r]);
    return n;
}

[[nodiscard]] constexpr u64 partition_of(const wire::ViewSpec& v) noexcept {
    return partition_key(v.bucket, v.password, v.region);
}

// ---- fan-out frames --------------------------------------------------------------------------

namespace patch_mask {
inline constexpr u8 full = 1u << 0;     // membership changed or repair: send entry or removal
inline constexpr u8 players = 1u << 1;  // players + max_players
inline constexpr u8 flags = 1u << 2;
inline constexpr u8 name = 1u << 3;
}  // namespace patch_mask

struct FrameItem {
    u32 handle = 0;
    u8 mask = 0;
};

// Layout-compatible with QUIC_BUFFER (checked in the QUIC layer).
struct BufferRef {
    u32 length = 0;
    u8* data = nullptr;
};

// One encoded Delta datagram for one view, shared by every subscriber of that view.
// Lifetime: `shard_refs` starts at the number of shards; each shard drops one reference when
// its last send of this frame reaches a final state.
struct Frame {
    std::atomic<u32> shard_refs{0};
    u32 view_id = 0;
    u64 vseq = 0;
    u64 created_ns = 0;  // replica time of the mutation, for delivery latency metrics
    inplace_vector<FrameItem, 2> items;
    BufferRef buf;

    [[nodiscard]] static Frame* make(std::span<const u8> bytes) {
        void* mem = ::operator new(sizeof(Frame) + bytes.size());
        auto* f = new (mem) Frame();
        f->buf.length = static_cast<u32>(bytes.size());
        f->buf.data = reinterpret_cast<u8*>(f + 1);
        std::memcpy(f->buf.data, bytes.data(), bytes.size());
        return f;
    }

    static void release(Frame* f) noexcept {
        if (f->shard_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            f->~Frame();
            ::operator delete(f);
        }
    }
};

// Immutable membership of one view's window, published for shards that must tell a lagging
// client which entries are still in the window (WindowSync).
struct WindowList {
    u64 vseq = 0;
    std::vector<u32> handles;
};

// Encoded snapshot frame for one (view, vseq), shared by every subscriber that asks for it.
struct SnapshotBlob {
    u32 view_id = 0;
    u64 vseq = 0;
    std::vector<u8> frame;       // FrameType::snapshot
    std::vector<u8> zstd_frame;  // FrameType::snapshot_zstd, empty if compression did not pay off
};

}  // namespace sb::registry
