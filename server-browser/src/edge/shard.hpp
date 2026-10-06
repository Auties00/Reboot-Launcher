#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <stop_token>
#include <vector>

#include "core/broadcast_ring.hpp"
#include "core/flat_map.hpp"
#include "core/inplace_vector.hpp"
#include "core/mailbox.hpp"
#include "core/rate_limit.hpp"
#include "core/slot_map.hpp"
#include "core/timer_wheel.hpp"
#include "core/waker.hpp"
#include "edge/dirty_set.hpp"
#include "edge/config.hpp"
#include "ops/metrics.hpp"
#include "quic/msquic.hpp"
#include "registry/events.hpp"
#include "registry/replica.hpp"
#include "registry/search_worker.hpp"
#include "security/crypto.hpp"
#include "wire/frame.hpp"

namespace sb::edge {

class GeoIp;

// Pseudo-handle marking a WindowSync in flush and repair bookkeeping (real handles are far smaller).
inline constexpr u32 kSyncHandle = ~u32{0};

// Everything a shard needs from the process; immutable after startup except where atomic.
struct EdgeContext {
    const EdgeConfig& cfg;
    const QUIC_API_TABLE* api = nullptr;
    std::atomic<HQUIC> configuration{nullptr};  // swapped on certificate reload
    registry::Replica& replica;
    std::vector<registry::SearchWorker*> search;
    AtomicRateTable conn_ip_rate;
    AtomicRateTable conn_subnet_rate;
    AtomicRateTable join_rate;
    security::Digest pepper{};
    std::vector<u8> ticket_key;
    std::atomic<std::shared_ptr<const GeoIp>> geoip;  // swapped on reload
    std::atomic<bool> draining{false};
    std::atomic<u64> next_search{0};

    EdgeContext(const EdgeConfig& c, registry::Replica& r)
        : cfg(c),
          replica(r),
          conn_ip_rate(1u << 20, RateSpec::per_second(c.limits.conn_per_ip_per_sec, c.limits.conn_per_ip_burst)),
          conn_subnet_rate(1u << 16, RateSpec::per_second(c.limits.conn_per_subnet_per_sec, c.limits.conn_per_subnet_burst)),
          join_rate(1u << 20, RateSpec::per_minute(c.limits.join_per_min, c.limits.join_burst)) {}
};

struct ShardStats {
    ops::Counter conns_browser;
    ops::Counter conns_host;
    ops::Counter accepted;
    ops::Counter rejected;
    ops::Counter closed;
    ops::Counter dgram_direct;
    ops::Counter dgram_flush;
    ops::Counter dgram_lost;
    ops::Counter dgram_failed;
    ops::Counter dirty_marks;
    ops::Counter repairs;
    ops::Counter snapshots;
    ops::Counter window_syncs;
    ops::Counter ctrl_frames_in;
    ops::Counter ctrl_frames_out;
    ops::Counter rate_limited;
    ops::Counter protocol_errors;
    ops::Counter stalls;
    ops::Counter moved_conns;
    ops::Counter ring_events;
    ops::Histogram delivery_us;  // replica mutation -> datagram handed to MsQuic
};

class Shard;
struct FlushSend;

struct Sub {
    u32 sub_id = 0;
    wire::ViewSpec spec;
    u32 view_id = 0;  // 0 until the replica confirms the subscription
    u32 window = 0;
    u32 pos = 0;  // index in Shard::view_subs_[view_id]
    u32 dirty = 0;
    bool sync_pending = false;  // removals collapsed into one WindowSync not yet sent
};

// Owned by its home shard; MsQuic callbacks arriving on another shard are forwarded.
// Cache-line aligned: fan-out reads the fields up to `dirty` for every subscriber of every change.
struct alignas(64) Conn {
    // datagram fan-out
    bool closing = false;
    bool dgram_enabled = false;
    bool use_stream = false;     // fallback: deltas on a server-opened uni stream
    bool in_ready = false;
    u16 max_dgram = 0;
    u32 unsent = 0;              // sends handed to MsQuic that have not left yet
    u32 batch_pending = 0;
    HQUIC h = nullptr;
    HQUIC delta_stream = nullptr;
    DirtySet dirty;
    u64 last_progress_ms = 0;
    inplace_vector<Sub, 16> subs;
    u32 syncs_pending = 0;

    SlotHandle self;
    u16 home = 0;
    std::atomic<bool> moved{false};
    IpAddr addr;
    u16 port = 0;
    u64 accepted_ms = 0;

    wire::Role role = wire::Role::unknown;
    bool hello = false;
    u64 features = 0;

    HQUIC control = nullptr;
    wire::StreamFramer framer{16 * 1024};
    u32 control_backlog = 0;  // bytes handed to MsQuic not yet completed

    TokenBucket query_rate;
    TokenBucket update_rate;

    // hosting
    u32 host_handle = 0;
    Uuid host_id;
    bool host_pending = false;
    TimerNode ttl;
    TimerNode update_timer;
    std::optional<wire::HostUpdate> deferred_update;
    std::optional<std::optional<security::Digest>> deferred_password;
};

class Shard {
public:
    Shard(EdgeContext& ctx, u16 index, std::vector<Shard*>& all);
    ~Shard();
    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;

    // Creates the epoll queue and binds it to an MsQuic execution context.
    void bind_execution(QUIC_EXECUTION_CONFIG& cfg);
    void set_execution(QUIC_EXECUTION* exec) noexcept { exec_ = exec; }
    void run(std::stop_token stop);

    [[nodiscard]] Mailbox& mailbox() noexcept { return mailbox_; }
    [[nodiscard]] Waker& waker() noexcept { return waker_; }
    [[nodiscard]] ShardStats& stats() noexcept { return stats_; }
    [[nodiscard]] u16 index() const noexcept { return index_; }
    [[nodiscard]] std::size_t connections() const noexcept { return conns_.size(); }

    // Called from the listener callback, which runs on this shard's thread.
    QUIC_STATUS accept(HQUIC conn, const QUIC_NEW_CONNECTION_INFO& info);

    static thread_local Shard* current;

private:
    friend struct Callbacks;

    // event loop pieces
    bool process_ring();
    bool process_mailbox();
    bool flush_ready();
    void sweep(u64 now_ms);
    void issue_pending();
    void handle_reply(u64 conn, const registry::ReplyPayload& p);

    // connection events (home shard only)
    void on_conn_event(Conn& c, QUIC_CONNECTION_EVENT& ev);
    void on_stream_event(Conn& c, HQUIC stream, void* sctx, QUIC_STREAM_EVENT& ev);
    void on_control_bytes(Conn& c, std::span<const u8> data);
    bool on_frame(Conn& c, const wire::FrameView& f);
    void on_datagram(Conn& c, std::span<const u8> data);
    void on_ring_drain(u64 phase);
    void fanout(registry::Frame* f);
    void repair(Conn& c, void* ctx, bool is_flush);
    void finish_send(Conn& c, void* ctx, bool is_flush);
    void on_timer(TimerNode* n);
    void on_dgram_state(Conn& c, void* ctx, QUIC_DATAGRAM_SEND_STATE st);
    void on_shutdown_complete(Conn& c);

    // control requests
    void on_hello(Conn& c, const wire::Hello& m);
    void on_subscribe(Conn& c, const wire::Subscribe& m);
    void on_unsubscribe(Conn& c, const wire::Unsubscribe& m);
    void on_query(Conn& c, wire::Query& m);
    void on_resolve(Conn& c, const wire::Resolve& m);
    void on_join(Conn& c, wire::Join& m);
    void on_host_register(Conn& c, wire::HostRegister& m);
    void on_host_update(Conn& c, wire::HostUpdate& m);
    void on_host_unregister(Conn& c, const wire::HostUnregister& m);
    void send_host_update(Conn& c, wire::HostUpdate& m, std::optional<std::optional<security::Digest>> pwd);

    // sending
    template <class T>
    void send(Conn& c, const T& msg);
    void send_raw(Conn& c, std::vector<u8> bytes);
    void send_error(Conn& c, u32 req_id, wire::ErrorCode code, std::string_view msg, u32 retry_ms = 0);
    void send_snapshot(Conn& c, std::shared_ptr<const registry::SnapshotBlob> blob);
    void mark_dirty(Conn& c, u32 view_id, u32 handle, u8 mask);
    void mark_dirty(Conn& c, Sub& s, u32 handle, u8 mask);
    void schedule_flush(Conn& c);
    void flush(Conn& c);
    void release_frame(registry::Frame* f);
    FlushSend* acquire_flush();
    void release_flush(FlushSend* fs);
    void close(Conn& c, u64 app_error, std::string_view reason);
    void drop_subscriptions(Conn& c);
    void begin_sync(Conn& c, Sub& s);
    void requeue(Conn& c, u64 key, u8 mask);
    [[nodiscard]] Sub* find_sub_by_view(Conn& c, u32 view_id) noexcept;
    [[nodiscard]] Conn* conn_of(u64 packed) noexcept;
    [[nodiscard]] u64 packed(const Conn& c) const noexcept { return c.self.pack(); }
    void process_forwarded(registry::ShardMsg* m);

    EdgeContext& ctx_;
    const QUIC_API_TABLE* api_;
    const u16 index_;
    std::vector<Shard*>& all_;
    ShardStats stats_;

    int epfd_ = -1;
    QUIC_EXECUTION* exec_ = nullptr;
    Waker waker_;
    Mailbox mailbox_{waker_};
    struct WakeSqe;
    std::unique_ptr<WakeSqe> wake_sqe_;
    BroadcastRing<registry::RingEvent*>::Consumer& consumer_;

    SlotMap<std::unique_ptr<Conn>> conns_;
    // A subscriber of a view: its Conn and the index of the Sub in conn->subs, so fan-out never
    // scans subs (whose size field sits past 640 bytes of inline storage, a sure cache miss).
    struct SubRef {
        Conn* conn;
        u32 sub;
    };
    // view id -> subscribers on this shard; entries leave in drop_subscriptions/on_unsubscribe
    // before their Conn is freed.
    std::vector<std::vector<SubRef>> view_subs_;
    std::vector<u64> view_vseq_;                       // latest vseq seen per view
    FlatMap<registry::Frame*, u32> frame_refs_;        // outstanding local sends per frame
    std::vector<std::pair<Conn*, registry::Frame*>> pending_;
    std::vector<SlotHandle> ready_;
    std::vector<std::pair<u64, u8>> scratch_dirty_;
    std::vector<std::pair<u64, u8>> flush_items_;  // items of the datagram being built
    wire::Writer patch_scratch_{256};
    std::vector<FlushSend*> flush_pool_;
    TimerWheel wheel_{100};
    u64 last_sweep_ms_ = 0;
    u64 now_ms_ = 0;
};

}  // namespace sb::edge
