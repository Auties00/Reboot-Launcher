#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <stop_token>
#include <unordered_map>
#include <vector>

#include "backbone/backbone.hpp"
#include "core/broadcast_ring.hpp"
#include "core/counted_btree.hpp"
#include "core/flat_map.hpp"
#include "core/hash.hpp"
#include "core/mpsc.hpp"
#include "core/timer_wheel.hpp"
#include "core/waker.hpp"
#include "registry/events.hpp"
#include "registry/model.hpp"
#include "registry/validation.hpp"

namespace sb::registry {

struct ReplicaConfig {
    u64 edge_id = 1;
    u32 max_entries = 1u << 20;
    u32 num_shards = 1;
    std::size_t ring_capacity = 1u << 16;
    u32 heartbeat_ms = 10'000;
    u32 ttl_ms = 30'000;
    u32 grace_ms = 15'000;
    u32 persist_interval_ms = 1'000;
    u32 probe_interval_ms = 300'000;
    u32 max_hosts_per_ip = 4;
    bool probe_enabled = true;
    bool zstd = true;
    std::vector<u32> windows{50, 200};
    u32 max_query_limit = 100;
    FieldLimits limits;
};

struct ReplicaStats {
    std::atomic<u64> mutations{0};
    std::atomic<u64> frames{0};
    std::atomic<u64> ring_stalls{0};
    std::atomic<u64> entries{0};
    std::atomic<u64> views{0};
    std::atomic<u64> lifecycle_conflicts{0};
};

// Name-aware cursor key for keyset paging; compared against SortKey heterogeneously.
struct CursorKey {
    u64 primary = 0;
    u32 handle = 0;
    std::string_view folded_name;
};

class Replica;

struct KeyLess {
    const Replica* rep = nullptr;
    bool by_name = false;

    [[nodiscard]] std::string_view name_of(u32 handle) const noexcept;

    template <class A, class B>
    [[nodiscard]] bool operator()(const A& a, const B& b) const noexcept {
        if (a.primary != b.primary) return a.primary < b.primary;
        if (by_name && a.handle != b.handle) {
            const int c = name(a).compare(name(b));
            if (c != 0) return c < 0;
        }
        return a.handle < b.handle;
    }

private:
    [[nodiscard]] std::string_view name(const SortKey& k) const noexcept { return name_of(k.handle); }
    [[nodiscard]] static std::string_view name(const CursorKey& k) noexcept { return k.folded_name; }
};

using SortTree = CountedBTree<SortKey, KeyLess>;

struct Partition {
    u64 key = 0;
    SortTree trees[3];
    std::vector<u32> views[3];  // view ids attached to each sort's tree

    explicit Partition(const Replica* rep, u64 k)
        : key(k), trees{SortTree(KeyLess{rep, false}), SortTree(KeyLess{rep, false}), SortTree(KeyLess{rep, true})} {}
};

struct View {
    u32 id = 0;
    u64 pkey = 0;
    wire::Sort sort{};
    u32 window = 0;
    u64 vseq = 0;
    u32 subscribers = 0;
    Partition* part = nullptr;
    std::shared_ptr<const SnapshotBlob> snapshot;  // valid while snapshot->vseq == vseq
};

// The single writer of registry state. Owns records and indexes, applies local requests and
// backbone deliveries, and publishes immutable entries and pre-encoded delta frames to the ring.
class Replica {
public:
    using ProbeFn = std::function<void(u32 handle, const IpAddr& addr, u16 port)>;

    Replica(ReplicaConfig cfg, backbone::Backbone& backbone);
    ~Replica();
    Replica(const Replica&) = delete;
    Replica& operator=(const Replica&) = delete;

    [[nodiscard]] BroadcastRing<RingEvent*>& ring() noexcept { return ring_; }
    [[nodiscard]] const ReplicaConfig& config() const noexcept { return cfg_; }
    [[nodiscard]] ReplicaStats& stats() noexcept { return stats_; }
    void set_probe(ProbeFn fn) { probe_ = std::move(fn); }

    // Thread-safe entry points.
    void post(ReplicaMsg* m) noexcept {
        inbox_.push(m);
        waker_.wake();
    }
    void post(u16 shard, u64 conn, ReplicaPayload p) { post(new ReplicaMsg(shard, conn, std::move(p))); }

    // Latest published state of an entry; safe from ring consumers (see BroadcastRing).
    [[nodiscard]] const PubEntry* pub(u32 handle) const noexcept {
        return handle < cfg_.max_entries ? pub_table_[handle].load(std::memory_order_acquire) : nullptr;
    }

    void run(std::stop_token stop);
    // One iteration: drains the inbox, fires timers, reclaims ring slots. Returns true if any
    // request was processed. Used by run() and directly by tests.
    bool poll_once(u64 now_ms);
    // Publishes a drain marker (replica thread only; others post a DrainReq).
    void publish_drain(u32 phase);

    // Replica-thread accessors (tests).
    [[nodiscard]] std::size_t size() const noexcept { return by_id_.size(); }
    [[nodiscard]] const Record* find(const Uuid& id) const;
    [[nodiscard]] std::string_view folded_name(u32 handle) const noexcept {
        return records_[handle] ? std::string_view(records_[handle]->folded_name) : std::string_view();
    }

private:
    struct PendingOp {
        enum class Kind : u8 { reg, update, unregister, tombstone, persist } kind{};
        u16 shard = kNoShard;
        u64 conn = 0;
        u32 req_id = 0;
        Uuid id;
        bool return_token = false;
        wire::Token token{};
        u64 acked_seq = 0;  // set when the ack arrived before the record
        std::optional<wire::HostUpdate> soft_tail;  // soft fields to apply once the lifecycle write lands
    };
    struct OwnerInfo {
        Digest token_hash{};
        u64 last_seq = 0;
        u64 expires_ms = 0;
        u32 epoch = 0;
    };
    struct RecordTimers {
        TimerNode grace;
        TimerNode persist;
        TimerNode probe;
    };

    void handle(ReplicaMsg& m, u64 now_ms);
    void on(u16 shard, u64 conn, SubscribeReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, UnsubscribeReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, QueryReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, ResolveReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, JoinReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, HostRegisterReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, HostUpdateReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, HostUnregisterReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, HostGoneReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, ProbeResultReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, DrainReq& r, u64 now_ms);
    void on(u16 shard, u64 conn, BackboneRecord& r, u64 now_ms);
    void on(u16 shard, u64 conn, BackboneSoft& r, u64 now_ms);
    void on(u16 shard, u64 conn, BackboneAck& r, u64 now_ms);
    void on(u16 shard, u64 conn, BackboneEdgeDown& r, u64 now_ms);

    // Mutation core: applies `change` to r and emits index updates, window membership changes,
    // delta frames and the new PubEntry as one ring event.
    template <class F>
    void mutate(Record& r, u64 now_ms, F&& change, bool deleting = false);
    void remove_record(Record& r, u64 now_ms);
    Record& create_record(const Uuid& id);

    Partition& partition(u64 key);
    View& view_for(const wire::ViewSpec& spec, u32 window);
    void attach_initial_members(View& v);
    std::shared_ptr<const SnapshotBlob> snapshot_of(View& v);
    void republish(Record& r, RingEvent& ev, bool fields_changed);

    void reply(u16 shard, u64 conn, ReplyPayload p);
    void reply_error(u16 shard, u64 conn, u32 req_id, wire::ErrorCode code, std::string msg, u32 retry_ms = 0);
    void publish_event(RingEvent* ev);
    void reclaim();

    [[nodiscard]] Record* by_handle(u32 h) noexcept { return h < records_.size() ? records_[h].get() : nullptr; }
    [[nodiscard]] Record* owned_by(u32 handle, u16 shard, u64 conn) noexcept;
    [[nodiscard]] backbone::ReplicatedRecord to_replicated(const Record& r) const;
    void apply_replicated(Record& r, const backbone::ReplicatedRecord& rr, bool soft_too);
    void apply_soft(Record& r, const backbone::SoftUpdate& su, u64 now_ms);
    void write_record(const Record& r, const backbone::ReplicatedRecord& rr, PendingOp op);
    void write_tombstone(const Record& r, PendingOp op);
    void publish_soft(Record& r, const backbone::SoftUpdate& su, u64 now_ms);
    void schedule_persist(Record& r, u64 now_ms);
    void complete_pending_for(const Uuid& id, u64 applied_seq, u64 now_ms);
    [[nodiscard]] bool has_pending(const Uuid& id) const;
    void finish_op(PendingOp& op, u64 now_ms);
    void fire_timer(TimerNode* n, u64 now_ms);
    void request_probe(Record& r, u64 now_ms);
    void ip_count(const IpAddr& a, int delta);

    friend struct KeyLess;

    ReplicaConfig cfg_;
    backbone::Backbone& backbone_;
    ProbeFn probe_;
    ReplicaStats stats_;

    MpscQueue inbox_;
    Waker waker_;
    BroadcastRing<RingEvent*> ring_;
    std::deque<std::pair<u64, RingEvent*>> in_flight_;

    std::vector<std::unique_ptr<Record>> records_;  // indexed by handle
    std::vector<std::unique_ptr<RecordTimers>> timers_;
    std::unique_ptr<std::atomic<const PubEntry*>[]> pub_table_;
    std::deque<u32> free_handles_;
    u32 next_handle_ = 1;
    FlatMap<Uuid, u32, UuidHash> by_id_;
    FlatMap<Uuid, OwnerInfo, UuidHash> owners_;
    FlatMap<IpAddr, u32, IpHash> hosts_per_ip_;

    FlatMap<u64, std::unique_ptr<Partition>> partitions_;
    std::vector<std::unique_ptr<View>> views_;  // index = view id; 0 unused
    FlatMap<u64, u32> view_ids_;  // (pkey, sort, window) -> view id

    FlatMap<u64, PendingOp> pending_;
    // Soft updates that raced ahead of their record during a backbone replay.
    FlatMap<Uuid, backbone::SoftUpdate, UuidHash> early_soft_;
    std::deque<Uuid> early_order_;
    u64 next_op_ = 1;
    TimerWheel wheel_{100};
    bool wheel_started_ = false;
    u64 last_reclaim_ms_ = 0;
};

}  // namespace sb::registry
