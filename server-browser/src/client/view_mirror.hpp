#pragma once

// Client-side state of one subscribed view: the reference implementation of the delta rules
// in docs/PROTOCOL.md. Datagrams may be lost, duplicated or reordered; applying snapshots and
// patches through this class always converges to the server's window.

#include <algorithm>
#include <vector>

#include "core/flat_map.hpp"
#include "core/types.hpp"
#include "wire/messages.hpp"

namespace sb::client {

class ViewMirror {
public:
    struct Item {
        wire::ListEntry entry;
        bool present = false;
        u64 seq_member = 0;  // presence and the fields only a full entry carries
        u64 seq_players = 0;
        u64 seq_flags = 0;
        u64 seq_name = 0;
    };

    explicit ViewMirror(std::size_t max_buffered = 4096) : max_buffered_(max_buffered) {}

    void on_snapshot(const wire::Snapshot& s) {
        items_.clear();
        for (const auto& e : s.entries) {
            Item& it = items_[e.handle];
            it.entry = e;
            it.present = true;
            it.seq_member = it.seq_players = it.seq_flags = it.seq_name = s.vseq;
        }
        floor_ = s.vseq;
        total_ = s.total;
        have_snapshot_ = true;
        auto pending = std::move(buffered_);
        buffered_.clear();
        if (pending_sync_) apply_sync(*pending_sync_);
        pending_sync_.reset();
        for (const auto& p : pending) apply(p);
    }

    void on_delta(const wire::Delta& d) {
        if (!have_snapshot_) {
            if (d.sync) pending_sync_ = *d.sync;  // only the newest matters
            for (const auto& p : d.patches)
                if (buffered_.size() < max_buffered_) buffered_.push_back(p);
            return;
        }
        if (d.sync) apply_sync(*d.sync);
        for (const auto& p : d.patches) apply(p);
    }

    // Everything not listed left the window, unless the client already knows something newer.
    void apply_sync(const wire::WindowSync& s) {
        if (s.vseq <= floor_) return;
        std::vector<u64> members(s.handles);
        std::sort(members.begin(), members.end());
        for (auto& [h, it] : items_) {
            if (!it.present || it.seq_member >= s.vseq) continue;
            if (std::binary_search(members.begin(), members.end(), h)) continue;
            it.present = false;
            raise_all(it, s.vseq);
        }
    }

    void apply(const wire::Patch& p) {
        if (p.vseq <= floor_) return;
        Item& it = items_[p.handle];
        const u64 v = p.vseq;
        if (p.removed) {
            if (v > it.seq_member) {
                it.present = false;
                raise_all(it, v);
            }
            return;
        }
        if (p.entry) {
            // Full entries are also used for repair with an equal sequence, hence >=.
            if (v >= it.seq_member) {
                const wire::ListEntry& e = *p.entry;
                it.entry.handle = p.handle;
                it.entry.id = e.id;
                it.entry.author = e.author;
                it.entry.version = e.version;
                it.entry.bucket = e.bucket;
                it.entry.region = e.region;
                it.entry.created_ms = e.created_ms;
                it.present = true;
                it.seq_member = v;
                if (v >= it.seq_players) {
                    it.entry.players = e.players;
                    it.entry.max_players = e.max_players;
                    it.seq_players = v;
                }
                if (v >= it.seq_flags) {
                    it.entry.flags = e.flags;
                    it.seq_flags = v;
                }
                if (v >= it.seq_name) {
                    it.entry.name = e.name;
                    it.seq_name = v;
                }
            }
        }
        if ((p.players || p.max_players) && v > it.seq_players) {
            if (p.players) it.entry.players = *p.players;
            if (p.max_players) it.entry.max_players = *p.max_players;
            it.seq_players = v;
        }
        if (p.flags && v > it.seq_flags) {
            it.entry.flags = *p.flags;
            it.seq_flags = v;
        }
        if (p.name && v > it.seq_name) {
            it.entry.name = *p.name;
            it.seq_name = v;
        }
    }

    [[nodiscard]] bool has_snapshot() const noexcept { return have_snapshot_; }
    [[nodiscard]] u32 total() const noexcept { return total_; }

    [[nodiscard]] std::size_t size() const {
        std::size_t n = 0;
        for (const auto& [h, it] : items_) n += it.present;
        return n;
    }

    [[nodiscard]] const Item* find(u64 handle) const {
        auto it = items_.find(handle);
        return it == items_.end() || !it->second.present ? nullptr : &it->second;
    }

    // Entries in protocol order for `sort` (see Sort in proto/rbsb.proto).
    [[nodiscard]] std::vector<wire::ListEntry> sorted(wire::Sort sort) const {
        std::vector<wire::ListEntry> out;
        for (const auto& [h, it] : items_)
            if (it.present) out.push_back(it.entry);
        std::sort(out.begin(), out.end(), [&](const wire::ListEntry& a, const wire::ListEntry& b) {
            return less(sort, a, b);
        });
        return out;
    }

    [[nodiscard]] static bool less(wire::Sort sort, const wire::ListEntry& a, const wire::ListEntry& b) {
        switch (sort) {
            case wire::Sort::players:
                if (a.players != b.players) return a.players > b.players;
                if (a.created_ms / 1000 != b.created_ms / 1000) return a.created_ms / 1000 > b.created_ms / 1000;
                break;
            case wire::Sort::newest:
                if (a.created_ms != b.created_ms) return a.created_ms > b.created_ms;
                break;
            case wire::Sort::name: {
                const int c = fold_compare(a.name, b.name);
                if (c != 0) return c < 0;
                break;
            }
        }
        return a.handle < b.handle;
    }

private:
    static int fold_compare(std::string_view a, std::string_view b) {
        const std::size_t n = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            const auto ca = static_cast<u8>(a[i] >= 'A' && a[i] <= 'Z' ? a[i] - 'A' + 'a' : a[i]);
            const auto cb = static_cast<u8>(b[i] >= 'A' && b[i] <= 'Z' ? b[i] - 'A' + 'a' : b[i]);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
    }

    static void raise_all(Item& it, u64 v) {
        it.seq_member = std::max(it.seq_member, v);
        it.seq_players = std::max(it.seq_players, v);
        it.seq_flags = std::max(it.seq_flags, v);
        it.seq_name = std::max(it.seq_name, v);
    }

    FlatMap<u64, Item> items_;
    std::vector<wire::Patch> buffered_;
    std::optional<wire::WindowSync> pending_sync_;
    std::size_t max_buffered_;
    u64 floor_ = 0;
    u32 total_ = 0;
    bool have_snapshot_ = false;
};

}  // namespace sb::client
