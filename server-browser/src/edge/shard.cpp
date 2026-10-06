#include "edge/shard.hpp"

#include <pthread.h>
#include <sched.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <algorithm>
#include <random>

#include "core/time.hpp"
#include "edge/geoip.hpp"
#include "ops/log.hpp"
#include "registry/validation.hpp"

namespace sb::edge {

using namespace registry;

thread_local Shard* Shard::current = nullptr;

namespace {

constexpr std::uintptr_t kFlushTag = 1;

enum class StreamKind : u8 { control, snapshot, delta, rejected };

struct StreamCtx {
    Conn* conn;
    StreamKind kind;
};

// Control-stream response buffer.
struct CtrlSend {
    QUIC_BUFFER qb{};
    std::vector<u8> bytes;
};

// Snapshot stream send: keeps the shared blob alive until MsQuic is done with it.
struct SnapSend {
    QUIC_BUFFER qb{};
    std::shared_ptr<const SnapshotBlob> blob;
};

// Per-connection coalesced datagram (or delta-stream chunk) built from current values.
struct FlushSend {
    QUIC_BUFFER qb{};
    std::vector<u8> bytes;
    std::vector<std::pair<u64, u8>> items;  // (view << 32 | handle, mask) for loss repair
};

void* tag_flush(FlushSend* f) { return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(f) | kFlushTag); }
bool is_flush(void* ctx) { return reinterpret_cast<std::uintptr_t>(ctx) & kFlushTag; }
void* untag(void* ctx) { return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(ctx) & ~kFlushTag); }

// Shard-internal mailbox message: a MsQuic event that fired on a foreign shard.
struct Fwd : ShardMsg {
    Conn* conn = nullptr;
    StreamCtx* sctx = nullptr;
    HQUIC stream = nullptr;
    bool is_stream = false;
    QUIC_CONNECTION_EVENT cev{};
    QUIC_STREAM_EVENT sev{};
    std::vector<u8> data;
    QUIC_BUFFER buf{};

    Fwd() : ShardMsg(0, {}) { node_kind = 1; }
};

std::vector<Shard*>* g_shards = nullptr;

u64 jitter(u64 max) {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    return max ? rng() % max : 0;
}

}  // namespace

// MsQuic callback thunks: dispatch to the home shard, forwarding when invoked elsewhere.
struct Callbacks {
    static QUIC_STATUS QUIC_API conn(HQUIC, void* context, QUIC_CONNECTION_EVENT* ev) {
        auto* c = static_cast<Conn*>(context);
        Shard* home = (*g_shards)[c->home];
        if (Shard::current == home && !c->moved.load(std::memory_order_relaxed)) {
            home->on_conn_event(*c, *ev);
            return QUIC_STATUS_SUCCESS;
        }
        c->moved.store(true, std::memory_order_relaxed);
        auto* f = new Fwd();
        f->conn = c;
        f->cev = *ev;
        if (ev->Type == QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED) {
            const QUIC_BUFFER* b = ev->DATAGRAM_RECEIVED.Buffer;
            f->data.assign(b->Buffer, b->Buffer + b->Length);
        } else if (ev->Type == QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED) {
            // The handler must be installed before returning; the home shard decides the stream's fate.
            auto* sc = new StreamCtx{c, StreamKind::rejected};
            home->api_->SetCallbackHandler(ev->PEER_STREAM_STARTED.Stream, reinterpret_cast<void*>(&Callbacks::stream), sc);
            f->sctx = sc;
        }
        home->mailbox().post(f);
        return QUIC_STATUS_SUCCESS;
    }

    static QUIC_STATUS QUIC_API stream(HQUIC stream, void* context, QUIC_STREAM_EVENT* ev) {
        auto* sc = static_cast<StreamCtx*>(context);
        Conn* c = sc->conn;
        Shard* home = (*g_shards)[c->home];
        if (Shard::current == home && !c->moved.load(std::memory_order_relaxed)) {
            home->on_stream_event(*c, stream, sc, *ev);
            return QUIC_STATUS_SUCCESS;
        }
        c->moved.store(true, std::memory_order_relaxed);
        auto* f = new Fwd();
        f->conn = c;
        f->sctx = sc;
        f->stream = stream;
        f->is_stream = true;
        f->sev = *ev;
        if (ev->Type == QUIC_STREAM_EVENT_RECEIVE) {
            for (u32 i = 0; i < ev->RECEIVE.BufferCount; ++i) {
                const QUIC_BUFFER& b = ev->RECEIVE.Buffers[i];
                f->data.insert(f->data.end(), b.Buffer, b.Buffer + b.Length);
            }
        }
        home->mailbox().post(f);
        return QUIC_STATUS_SUCCESS;
    }
};

struct Shard::WakeSqe {
    QUIC_SQE sqe;  // must stay first: epoll hands back &sqe
    Shard* shard;

    static void complete(QUIC_CQE* cqe) {
        auto* w = reinterpret_cast<WakeSqe*>(static_cast<QUIC_SQE*>(cqe->data.ptr));
        w->shard->waker_.drain();
    }
};

Shard::Shard(EdgeContext& ctx, u16 index, std::vector<Shard*>& all)
    : ctx_(ctx),
      api_(ctx.api),
      index_(index),
      all_(all),
      consumer_(ctx.replica.ring().add_consumer(&waker_)) {
    g_shards = &all_;
}

Shard::~Shard() {
    while (MpscNode* n = mailbox_.pop()) delete static_cast<ShardMsg*>(n);
    if (epfd_ >= 0) ::close(epfd_);
}

void Shard::bind_execution(QUIC_EXECUTION_CONFIG& cfg) {
    epfd_ = ::epoll_create1(EPOLL_CLOEXEC);
    SB_ASSERT(epfd_ >= 0);
    wake_sqe_ = std::make_unique<WakeSqe>();
    wake_sqe_->sqe.fd = waker_.fd();
    wake_sqe_->sqe.Completion = &WakeSqe::complete;
    wake_sqe_->shard = this;
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &wake_sqe_->sqe;
    // Contract predicates see locals as const, so keep the side effect outside.
    const int rc = ::epoll_ctl(epfd_, EPOLL_CTL_ADD, waker_.fd(), &ev);
    SB_ASSERT(rc == 0);
    cfg.IdealProcessor = ctx_.cfg.cpu_offset + index_;
    cfg.EventQ = &epfd_;
}

// ---- event loop ------------------------------------------------------------------------------

void Shard::run(std::stop_token stop) {
    current = this;
    if (ctx_.cfg.pin_shards) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET((ctx_.cfg.cpu_offset + index_) % CPU_SETSIZE, &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) != 0)
            log::warn("shard {}: could not pin to cpu {}", index_, ctx_.cfg.cpu_offset + index_);
    }
    std::stop_callback wake_on_stop(stop, [this] { waker_.wake(); });
    epoll_event events[128];
    now_ms_ = mono_ms();
    wheel_.start(now_ms_);
    last_sweep_ms_ = now_ms_;
    u64 stop_deadline = 0;
    for (;;) {
        now_ms_ = mono_ms();
        const u32 quic_wait = api_->ExecutionPoll(exec_);
        bool did = process_ring();
        did |= process_mailbox();
        did |= flush_ready();
        wheel_.advance(now_ms_, [this](TimerNode* n) { on_timer(n); });
        if (now_ms_ - last_sweep_ms_ >= 1000) {
            sweep(now_ms_);
            last_sweep_ms_ = now_ms_;
        }
        if (stop.stop_requested()) {
            if (!stop_deadline) stop_deadline = now_ms_ + 5000;
            if (conns_.size() == 0 || now_ms_ >= stop_deadline) break;
        }

        int timeout = 0;
        if (!did) {
            waker_.announce_sleep();
            if (ctx_.replica.ring().available() != consumer_.cursor.load(std::memory_order_relaxed) ||
                !mailbox_.empty() || !ready_.empty() || stop.stop_requested()) {
                waker_.cancel_sleep();
            } else {
                u64 t = std::min<u64>(wheel_.ms_to_next_tick(now_ms_), 1000);
                if (quic_wait != UINT32_MAX) t = std::min<u64>(t, quic_wait);
                timeout = static_cast<int>(t);
            }
        }
        const int n = ::epoll_wait(epfd_, events, 128, timeout);
        waker_.cancel_sleep();
        for (int i = 0; i < n; ++i) {
            auto* sqe = static_cast<QUIC_SQE*>(events[i].data.ptr);
            sqe->Completion(&events[i]);
        }
    }
    consumer_.active.store(false, std::memory_order_release);
    current = nullptr;
}

bool Shard::process_ring() {
    auto& ring = ctx_.replica.ring();
    const u64 avail = ring.available();
    u64 seq = consumer_.cursor.load(std::memory_order_relaxed);
    if (seq == avail) return false;
    // Bounded batch keeps the loop responsive to MsQuic under extreme mutation rates.
    const u64 end = std::min<u64>(avail, seq + 1024);
    for (; seq < end; ++seq) {
        const RingEvent* ev = ring.at(seq);
        stats_.ring_events.inc();
        switch (ev->kind) {
            case RingEvent::Kind::mutation:
                for (Frame* f : ev->frames) fanout(f);
                break;
            case RingEvent::Kind::reply:
                if (ev->target == index_) handle_reply(ev->conn, *ev->reply);
                break;
            case RingEvent::Kind::drain: on_ring_drain(ev->conn); break;
        }
    }
    issue_pending();
    BroadcastRing<RingEvent*>::advance(consumer_, seq);
    return true;
}

bool Shard::process_mailbox() {
    bool did = false;
    for (int budget = 256; budget > 0; --budget) {
        auto* m = static_cast<ShardMsg*>(mailbox_.pop());
        if (!m) break;
        did = true;
        if (m->node_kind == 1) process_forwarded(m);
        else handle_reply(m->conn, m->payload);
        delete m;
    }
    if (did) issue_pending();
    return did;
}

void Shard::process_forwarded(ShardMsg* m) {
    auto* f = static_cast<Fwd*>(m);
    Conn& c = *f->conn;
    if (f->is_stream) {
        if (f->sev.Type == QUIC_STREAM_EVENT_RECEIVE) {
            f->buf.Buffer = f->data.data();
            f->buf.Length = static_cast<u32>(f->data.size());
            f->sev.RECEIVE.Buffers = &f->buf;
            f->sev.RECEIVE.BufferCount = 1;
            f->sev.RECEIVE.TotalBufferLength = f->data.size();
        }
        on_stream_event(c, f->stream, f->sctx, f->sev);
        return;
    }
    if (f->cev.Type == QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED) {
        f->buf.Buffer = f->data.data();
        f->buf.Length = static_cast<u32>(f->data.size());
        f->cev.DATAGRAM_RECEIVED.Buffer = &f->buf;
    }
    if (f->cev.Type == QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED) {
        // Handler already installed by the forwarding thread; decide the stream's role here.
        const bool bidi = !(f->cev.PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL);
        if (bidi && !c.control) {
            f->sctx->kind = StreamKind::control;
            c.control = f->cev.PEER_STREAM_STARTED.Stream;
        } else {
            api_->StreamShutdown(f->cev.PEER_STREAM_STARTED.Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
        }
        stats_.moved_conns.inc();
        return;
    }
    on_conn_event(c, f->cev);
}

void Shard::sweep(u64 now_ms) {
    const auto& lim = ctx_.cfg.limits;
    conns_.for_each([&](SlotHandle, std::unique_ptr<Conn>& cp) {
        Conn& c = *cp;
        if (c.closing) return;
        if (!c.hello && now_ms - c.accepted_ms > lim.hello_timeout_ms) {
            close(c, 3, "hello timeout");
        } else if (c.unsent > 0 && now_ms - c.last_progress_ms > lim.stall_timeout_ms) {
            stats_.stalls.inc();
            close(c, 4, "stalled");
        }
    });
}

void Shard::on_timer(TimerNode* n) {
    Conn* c = conn_of(n->cookie);
    if (!c) return;
    if (n == &c->ttl) {
        if (c->host_handle) {
            ctx_.replica.post(index_, packed(*c), HostGoneReq{.handle = c->host_handle});
            c->host_handle = 0;
        }
        close(*c, 5, "heartbeat timeout");
    } else if (n == &c->update_timer && c->deferred_update) {
        if (!c->update_rate.take(now_ms_)) {
            wheel_.schedule(&c->update_timer, now_ms_ + c->update_rate.wait_ms(now_ms_));
            return;
        }
        wire::HostUpdate u = std::move(*c->deferred_update);
        auto pwd = std::move(c->deferred_password);
        c->deferred_update.reset();
        c->deferred_password.reset();
        send_host_update(*c, u, std::move(pwd));
    }
}

void Shard::on_ring_drain(u64 phase) {
    if (phase == 0) {
        const u32 spread = ctx_.cfg.drain_spread_ms;
        conns_.for_each([&](SlotHandle, std::unique_ptr<Conn>& c) {
            if (c->hello && !c->closing)
                send(*c, wire::GoAway{.reason = wire::GoAwayReason::shutdown, .reconnect_after_ms = static_cast<u32>(jitter(spread))});
        });
    } else {
        conns_.for_each([&](SlotHandle, std::unique_ptr<Conn>& c) { close(*c, 0, "shutdown"); });
    }
}

// ---- fan-out ---------------------------------------------------------------------------------

void Shard::fanout(Frame* f) {
    if (f->view_id >= view_vseq_.size()) view_vseq_.resize(f->view_id + 64, 0);
    view_vseq_[f->view_id] = std::max(view_vseq_[f->view_id], f->vseq);
    u32 local = 0;
    if (f->view_id < view_subs_.size()) {
        const u32 cap_dgram = ctx_.cfg.limits.max_unsent_datagrams;
        const u32 cap_stream = ctx_.cfg.limits.max_stream_inflight;
        for (SlotHandle h : view_subs_[f->view_id]) {
            Conn* c = conn_of(h.pack());
            if (!c || c->closing) continue;
            const u32 cap = c->use_stream ? cap_stream : cap_dgram;
            const bool can_send = c->use_stream ? c->delta_stream != nullptr : (c->dgram_enabled && f->buf.length <= c->max_dgram);
            if (can_send && c->dirty.empty() && c->unsent + c->batch_pending < cap) {
                pending_.emplace_back(c, f);
                ++c->batch_pending;
                ++local;
            } else {
                // Autocork: the connection is busy, remember what changed and send current values later.
                for (const FrameItem& it : f->items) mark_dirty(*c, f->view_id, it.handle, it.mask);
            }
        }
    }
    if (local == 0) {
        Frame::release(f);
        return;
    }
    frame_refs_[f] += local;
    stats_.delivery_us.observe_us((mono_ns() - f->created_ns) / 1000);
}

void Shard::issue_pending() {
    for (auto& [c, f] : pending_) {
        --c->batch_pending;
        QUIC_STATUS st;
        if (c->use_stream) {
            st = api_->StreamSend(c->delta_stream, quic::as_quic(&f->buf), 1, QUIC_SEND_FLAG_NONE, f);
        } else {
            // Several frames for one connection in this batch: let MsQuic pack them into one packet.
            const auto flags = c->batch_pending > 0 ? QUIC_SEND_FLAG_DELAY_SEND : QUIC_SEND_FLAG_NONE;
            st = api_->DatagramSend(c->h, quic::as_quic(&f->buf), 1, flags, f);
        }
        if (QUIC_SUCCEEDED(st)) {
            if (c->unsent++ == 0) c->last_progress_ms = now_ms_;
            stats_.dgram_direct.inc();
        } else {
            stats_.dgram_failed.inc();
            for (const FrameItem& it : f->items) mark_dirty(*c, f->view_id, it.handle, it.mask);
            release_frame(f);
        }
    }
    pending_.clear();
}

void Shard::release_frame(Frame* f) {
    auto it = frame_refs_.find(f);
    SB_DASSERT(it != frame_refs_.end());
    if (--it->second == 0) {
        frame_refs_.erase(it);
        Frame::release(f);
    }
}

Sub* Shard::find_sub_by_view(Conn& c, u32 view_id) noexcept {
    for (Sub& s : c.subs)
        if (s.view_id == view_id) return &s;
    return nullptr;
}

void Shard::mark_dirty(Conn& c, u32 view_id, u32 handle, u8 mask) {
    Sub* s = find_sub_by_view(c, view_id);
    if (!s || s->resyncing) return;
    stats_.dirty_marks.inc();
    if (c.dirty.mark(view_id, handle, mask) && ++s->dirty > 2 * s->window) {
        request_resync(c, *s);
        return;
    }
    if (c.unsent == 0) schedule_flush(c);
}

void Shard::schedule_flush(Conn& c) {
    if (c.in_ready || c.closing) return;
    c.in_ready = true;
    ready_.push_back(c.self);
}

bool Shard::flush_ready() {
    if (ready_.empty()) return false;
    std::vector<SlotHandle> batch;
    batch.swap(ready_);
    for (SlotHandle h : batch) {
        Conn* c = conn_of(h.pack());
        if (!c) continue;
        c->in_ready = false;
        flush(*c);
    }
    return true;
}

void Shard::request_resync(Conn& c, Sub& s) {
    // Too far behind: a cached snapshot is cheaper than replaying every change.
    s.resyncing = true;
    s.dirty = 0;
    c.dirty.erase_view(s.view_id);
    stats_.resnapshots.inc();
    ctx_.replica.post(index_, packed(c), SubscribeReq{.req_id = 0, .sub_id = s.sub_id, .spec = s.spec, .window = s.window, .resync = true});
}

void Shard::flush(Conn& c) {
    if (c.closing || c.dirty.empty()) return;
    const u32 cap = c.use_stream ? ctx_.cfg.limits.max_stream_inflight : ctx_.cfg.limits.max_unsent_datagrams;
    if (c.unsent >= cap) return;  // resumed when a send completes
    std::size_t limit;
    if (c.use_stream) {
        if (!c.delta_stream) return;
        limit = 16 * 1024;
    } else {
        if (!c.dgram_enabled || c.max_dgram < 128) return;
        limit = c.max_dgram;
    }

    c.dirty.take_sorted(scratch_dirty_);
    for (Sub& s : c.subs) s.dirty = 0;

    wire::Writer out(limit);
    wire::Writer pw(256);
    std::vector<std::pair<u64, u8>> items;
    u32 cur_view = 0;
    std::size_t frame_at = 0;
    auto close_frame = [&] {
        if (!cur_view) return;
        const std::size_t len = out.size() - frame_at - 4;
        out.data()[frame_at + 2] = static_cast<u8>(0x40 | (len >> 8));
        out.data()[frame_at + 3] = static_cast<u8>(len);
        cur_view = 0;
    };
    auto emit = [&]() -> bool {
        close_frame();
        if (out.size() == 0) return true;
        auto* fs = new FlushSend();
        fs->bytes = out.take();
        fs->items = std::move(items);
        items.clear();
        fs->qb.Buffer = fs->bytes.data();
        fs->qb.Length = static_cast<u32>(fs->bytes.size());
        const QUIC_STATUS st = c.use_stream
                                   ? api_->StreamSend(c.delta_stream, &fs->qb, 1, QUIC_SEND_FLAG_NONE, tag_flush(fs))
                                   : api_->DatagramSend(c.h, &fs->qb, 1, QUIC_SEND_FLAG_NONE, tag_flush(fs));
        out = wire::Writer(limit);
        if (QUIC_FAILED(st)) {
            stats_.dgram_failed.inc();
            for (auto [k, m] : fs->items) c.dirty.mark(static_cast<u32>(k >> 32), static_cast<u32>(k), m);
            delete fs;
            return false;
        }
        if (c.unsent++ == 0) c.last_progress_ms = now_ms_;
        stats_.dgram_flush.inc();
        return c.unsent < cap;
    };

    std::size_t i = 0;
    for (; i < scratch_dirty_.size(); ++i) {
        const auto [key, mask] = scratch_dirty_[i];
        const u32 view = static_cast<u32>(key >> 32);
        const u32 handle = static_cast<u32>(key);
        Sub* s = find_sub_by_view(c, view);
        if (!s || s->resyncing) continue;

        wire::Patch p{.handle = handle, .vseq = view < view_vseq_.size() ? view_vseq_[view] : 0};
        const PubEntry* pub = ctx_.replica.pub(handle);
        if (!pub || !pub->in_view(view)) {
            p.removed = true;
        } else if (mask & patch_mask::full) {
            pub->to_list_entry(p.entry.emplace());
        } else {
            if (mask & patch_mask::players) {
                p.players = pub->players;
                p.max_players = pub->max_players;
            }
            if (mask & patch_mask::flags) p.flags = pub->flags;
            if (mask & patch_mask::name) p.name = pub->name;
        }
        pw.clear();
        wire::encode(pw, p);
        const std::size_t patch_bytes = 1 + wire::varint_size(pw.size()) + pw.size();
        const std::size_t frame_overhead = cur_view == view ? 0 : 4 + 1 + wire::varint_size(view);
        if (out.size() + frame_overhead + patch_bytes > limit) {
            if (out.size() == 0) continue;  // single patch larger than a datagram: impossible by limits
            if (!emit()) break;
        }
        if (cur_view != view) {
            close_frame();
            frame_at = out.size();
            out.quic_varint(static_cast<u64>(wire::FrameType::delta));  // 2 bytes
            out.put(u8{0x40});
            out.put(u8{0});  // length patched by close_frame
            out.put(u8{0x08});  // field 1: view_id
            out.varint(view);
            cur_view = view;
        }
        out.put(u8{0x12});  // field 2: patches
        out.varint(pw.size());
        out.put(pw.view());
        items.emplace_back(key, mask);
    }
    if (i == scratch_dirty_.size()) {
        emit();
    } else {
        // Out of send budget: keep the rest dirty for the next completion.
        for (; i < scratch_dirty_.size(); ++i) {
            const auto [key, mask] = scratch_dirty_[i];
            if (Sub* s = find_sub_by_view(c, static_cast<u32>(key >> 32)); s && !s->resyncing) {
                c.dirty.mark(static_cast<u32>(key >> 32), static_cast<u32>(key), mask);
                ++s->dirty;
            }
        }
        for (auto [k, m] : items) c.dirty.mark(static_cast<u32>(k >> 32), static_cast<u32>(k), m);
    }
}

void Shard::repair(Conn& c, void* ctx, bool flush_ctx) {
    stats_.repairs.inc();
    if (flush_ctx) {
        for (auto [k, m] : static_cast<FlushSend*>(ctx)->items) mark_dirty(c, static_cast<u32>(k >> 32), static_cast<u32>(k), m);
    } else {
        const auto* f = static_cast<Frame*>(ctx);
        for (const FrameItem& it : f->items) mark_dirty(c, f->view_id, it.handle, it.mask);
    }
}

void Shard::finish_send(Conn& c, void* ctx, bool flush_ctx) {
    (void)c;
    if (flush_ctx) delete static_cast<FlushSend*>(ctx);
    else release_frame(static_cast<Frame*>(ctx));
}

void Shard::on_dgram_state(Conn& c, void* raw, QUIC_DATAGRAM_SEND_STATE st) {
    const bool fl = is_flush(raw);
    void* ctx = untag(raw);
    if (!ctx) return;
    switch (st) {
        case QUIC_DATAGRAM_SEND_SENT:
            if (c.unsent > 0) --c.unsent;
            c.last_progress_ms = now_ms_;
            if (!c.dirty.empty()) schedule_flush(c);
            break;
        case QUIC_DATAGRAM_SEND_LOST_SUSPECT:
            stats_.dgram_lost.inc();
            repair(c, ctx, fl);
            break;
        case QUIC_DATAGRAM_SEND_LOST_DISCARDED:
            repair(c, ctx, fl);
            finish_send(c, ctx, fl);
            break;
        case QUIC_DATAGRAM_SEND_CANCELED:
            if (c.unsent > 0) --c.unsent;
            if (!c.closing) repair(c, ctx, fl);
            finish_send(c, ctx, fl);
            break;
        case QUIC_DATAGRAM_SEND_ACKNOWLEDGED:
        case QUIC_DATAGRAM_SEND_ACKNOWLEDGED_SPURIOUS:
            finish_send(c, ctx, fl);
            break;
        default: break;
    }
}

// ---- connection events -----------------------------------------------------------------------

QUIC_STATUS Shard::accept(HQUIC h, const QUIC_NEW_CONNECTION_INFO& info) {
    const IpAddr ip = quic::ip_of(*info.RemoteAddress);
    const u64 now = mono_ms();
    if (ctx_.draining.load(std::memory_order_relaxed) ||
        !ctx_.conn_ip_rate.take(IpHash{}(ip), now) || !ctx_.conn_subnet_rate.take(IpHash{}(ip.subnet()), now)) {
        stats_.rejected.inc();
        return QUIC_STATUS_CONNECTION_REFUSED;
    }
    auto [sh, slot] = conns_.emplace(std::make_unique<Conn>());
    Conn& c = **slot;
    c.h = h;
    c.self = sh;
    c.home = index_;
    c.addr = ip;
    c.port = QuicAddrGetPort(info.RemoteAddress);
    c.accepted_ms = now;
    c.last_progress_ms = now;
    c.framer = wire::StreamFramer(ctx_.cfg.limits.max_frame);
    c.query_rate = TokenBucket(RateSpec::per_second(ctx_.cfg.limits.query_per_sec, ctx_.cfg.limits.query_burst));
    c.update_rate = TokenBucket(RateSpec::per_second(ctx_.cfg.limits.host_update_per_sec, ctx_.cfg.limits.host_update_burst));
    c.ttl.cookie = c.update_timer.cookie = sh.pack();
    api_->SetCallbackHandler(h, reinterpret_cast<void*>(&Callbacks::conn), &c);
    const QUIC_STATUS st = api_->ConnectionSetConfiguration(h, ctx_.configuration.load(std::memory_order_acquire));
    if (QUIC_FAILED(st)) {
        conns_.erase(sh);
        return st;
    }
    stats_.accepted.inc();
    return QUIC_STATUS_SUCCESS;
}

void Shard::on_conn_event(Conn& c, QUIC_CONNECTION_EVENT& ev) {
    switch (ev.Type) {
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            HQUIC s = ev.PEER_STREAM_STARTED.Stream;
            const bool bidi = !(ev.PEER_STREAM_STARTED.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL);
            auto* sc = new StreamCtx{&c, bidi && !c.control ? StreamKind::control : StreamKind::rejected};
            api_->SetCallbackHandler(s, reinterpret_cast<void*>(&Callbacks::stream), sc);
            if (sc->kind == StreamKind::control) c.control = s;
            else api_->StreamShutdown(s, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 1);
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
            c.dgram_enabled = ev.DATAGRAM_STATE_CHANGED.SendEnabled;
            c.max_dgram = ev.DATAGRAM_STATE_CHANGED.MaxSendLength;
            if (c.dgram_enabled && !c.dirty.empty()) schedule_flush(c);
            break;
        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
            on_datagram(c, std::span<const u8>(ev.DATAGRAM_RECEIVED.Buffer->Buffer, ev.DATAGRAM_RECEIVED.Buffer->Length));
            break;
        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
            on_dgram_state(c, ev.DATAGRAM_SEND_STATE_CHANGED.ClientContext, ev.DATAGRAM_SEND_STATE_CHANGED.State);
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER: c.closing = true; break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE: on_shutdown_complete(c); break;
        default: break;
    }
}

void Shard::on_stream_event(Conn& c, HQUIC stream, void* sctx_raw, QUIC_STREAM_EVENT& ev) {
    auto* sctx = static_cast<StreamCtx*>(sctx_raw);
    const StreamKind kind = sctx->kind;
    switch (ev.Type) {
        case QUIC_STREAM_EVENT_RECEIVE:
            if (kind == StreamKind::control) {
                for (u32 i = 0; i < ev.RECEIVE.BufferCount && !c.closing; ++i)
                    on_control_bytes(c, std::span<const u8>(ev.RECEIVE.Buffers[i].Buffer, ev.RECEIVE.Buffers[i].Length));
            }
            break;
        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            void* ctx = ev.SEND_COMPLETE.ClientContext;
            if (kind == StreamKind::control) {
                auto* cs = static_cast<CtrlSend*>(ctx);
                c.control_backlog -= std::min<u32>(c.control_backlog, cs->qb.Length);
                delete cs;
            } else if (kind == StreamKind::delta) {
                if (c.unsent > 0) --c.unsent;
                c.last_progress_ms = now_ms_;
                finish_send(c, untag(ctx), is_flush(ctx));
                if (!c.dirty.empty()) schedule_flush(c);
            } else {
                delete static_cast<SnapSend*>(ctx);
            }
            break;
        }
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
            if (kind == StreamKind::control) close(c, 0, "control stream closed");
            break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (stream == c.control) c.control = nullptr;
            if (stream == c.delta_stream) c.delta_stream = nullptr;
            api_->StreamClose(stream);
            delete sctx;
            break;
        default: break;
    }
}

void Shard::on_shutdown_complete(Conn& c) {
    drop_subscriptions(c);
    if (c.host_handle) {
        ctx_.replica.post(index_, packed(c), HostGoneReq{.handle = c.host_handle, .draining = ctx_.draining.load(std::memory_order_relaxed)});
        c.host_handle = 0;
    }
    wheel_.cancel(&c.ttl);
    wheel_.cancel(&c.update_timer);
    if (c.role == wire::Role::browser) stats_.conns_browser.set(stats_.conns_browser.get() - 1);
    else if (c.role == wire::Role::host) stats_.conns_host.set(stats_.conns_host.get() - 1);
    stats_.closed.inc();
    api_->ConnectionClose(c.h);
    conns_.erase(c.self);
}

void Shard::close(Conn& c, u64 app_error, std::string_view reason) {
    if (c.closing) return;
    c.closing = true;
    if (!reason.empty())
        (void)api_->SetParam(c.h, QUIC_PARAM_CONN_CLOSE_REASON_PHRASE, static_cast<u32>(reason.size()), reason.data());
    api_->ConnectionShutdown(c.h, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, app_error);
}

void Shard::drop_subscriptions(Conn& c) {
    for (Sub& s : c.subs) {
        if (!s.view_id) continue;
        auto& vec = view_subs_[s.view_id];
        const u32 pos = s.pos;
        const SlotHandle moved = vec.back();
        vec[pos] = moved;
        vec.pop_back();
        if (pos < vec.size())
            if (Conn* o = conn_of(moved.pack()))
                if (Sub* os = find_sub_by_view(*o, s.view_id)) os->pos = pos;
        ctx_.replica.post(index_, packed(c), UnsubscribeReq{.view_id = s.view_id});
    }
    c.subs.clear();
    c.dirty = DirtySet{};
}

Conn* Shard::conn_of(u64 p) noexcept {
    auto* slot = conns_.get(SlotHandle::unpack(p));
    return slot ? slot->get() : nullptr;
}

// ---- control protocol ------------------------------------------------------------------------

template <class T>
void Shard::send(Conn& c, const T& msg) {
    wire::Writer w;
    wire::encode_frame(w, msg);
    send_raw(c, w.take());
}

void Shard::send_raw(Conn& c, std::vector<u8> bytes) {
    if (!c.control || c.closing) return;
    if (c.control_backlog + bytes.size() > ctx_.cfg.limits.max_control_backlog) {
        close(c, 6, "control backlog exceeded");
        return;
    }
    auto* cs = new CtrlSend();
    cs->bytes = std::move(bytes);
    cs->qb.Buffer = cs->bytes.data();
    cs->qb.Length = static_cast<u32>(cs->bytes.size());
    if (QUIC_FAILED(api_->StreamSend(c.control, &cs->qb, 1, QUIC_SEND_FLAG_NONE, cs))) {
        delete cs;
        return;
    }
    c.control_backlog += cs->qb.Length;
    stats_.ctrl_frames_out.inc();
}

void Shard::send_error(Conn& c, u32 req_id, wire::ErrorCode code, std::string_view msg, u32 retry_ms) {
    send(c, wire::Error{.req_id = req_id, .code = code, .message = std::string(msg), .retry_after_ms = retry_ms});
}

void Shard::send_snapshot(Conn& c, std::shared_ptr<const SnapshotBlob> blob) {
    HQUIC s = nullptr;
    auto* sc = new StreamCtx{&c, StreamKind::snapshot};
    if (QUIC_FAILED(api_->StreamOpen(c.h, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, &Callbacks::stream, sc, &s))) {
        delete sc;
        return;
    }
    auto* ss = new SnapSend();
    const bool z = (c.features & wire::feature::zstd) && !blob->zstd_frame.empty();
    const auto& bytes = z ? blob->zstd_frame : blob->frame;
    ss->qb.Buffer = const_cast<u8*>(bytes.data());
    ss->qb.Length = static_cast<u32>(bytes.size());
    ss->blob = std::move(blob);
    if (QUIC_FAILED(api_->StreamSend(s, &ss->qb, 1, QUIC_SEND_FLAG_START | QUIC_SEND_FLAG_FIN, ss))) {
        delete ss;
        api_->StreamClose(s);
        delete sc;
        return;
    }
    stats_.snapshots.inc();
}

void Shard::on_control_bytes(Conn& c, std::span<const u8> data) {
    const auto st = c.framer.feed(data, [&](const wire::FrameView& f) {
        stats_.ctrl_frames_in.inc();
        return on_frame(c, f);
    });
    if (st != wire::StreamFramer::Status::ok) {
        stats_.protocol_errors.inc();
        close(c, 1, st == wire::StreamFramer::Status::too_large ? "frame too large" : "malformed frame");
    }
}

bool Shard::on_frame(Conn& c, const wire::FrameView& f) {
    using wire::FrameType;
    if (c.closing) return true;
    if (!c.hello) {
        wire::Hello m;
        if (!wire::decode_frame(f, m)) return false;
        on_hello(c, m);
        return true;
    }
    switch (f.type) {
        case FrameType::subscribe: {
            wire::Subscribe m;
            if (!wire::decode_frame(f, m)) return false;
            on_subscribe(c, m);
            return true;
        }
        case FrameType::unsubscribe: {
            wire::Unsubscribe m;
            if (!wire::decode_frame(f, m)) return false;
            on_unsubscribe(c, m);
            return true;
        }
        case FrameType::query: {
            wire::Query m;
            if (!wire::decode_frame(f, m)) return false;
            on_query(c, m);
            return true;
        }
        case FrameType::resolve: {
            wire::Resolve m;
            if (!wire::decode_frame(f, m)) return false;
            on_resolve(c, m);
            return true;
        }
        case FrameType::join: {
            wire::Join m;
            if (!wire::decode_frame(f, m)) return false;
            on_join(c, m);
            return true;
        }
        case FrameType::host_register: {
            wire::HostRegister m;
            if (!wire::decode_frame(f, m)) return false;
            on_host_register(c, m);
            return true;
        }
        case FrameType::host_update: {
            wire::HostUpdate m;
            if (!wire::decode_frame(f, m)) return false;
            on_host_update(c, m);
            return true;
        }
        case FrameType::host_unregister: {
            wire::HostUnregister m;
            if (!wire::decode_frame(f, m)) return false;
            on_host_unregister(c, m);
            return true;
        }
        case FrameType::host_heartbeat:
            if (c.host_handle) wheel_.schedule(&c.ttl, now_ms_ + ctx_.cfg.registry.ttl_ms);
            return true;
        default:
            send_error(c, 0, wire::ErrorCode::unsupported, "unknown frame type");
            return true;
    }
}

void Shard::on_datagram(Conn& c, std::span<const u8> data) {
    (void)wire::for_each_frame(data, 1500, [&](const wire::FrameView& f) {
        if (f.type == wire::FrameType::host_heartbeat && c.host_handle)
            wheel_.schedule(&c.ttl, now_ms_ + ctx_.cfg.registry.ttl_ms);
        return true;
    });
}

void Shard::on_hello(Conn& c, const wire::Hello& m) {
    if (m.role != wire::Role::browser && m.role != wire::Role::host) {
        stats_.protocol_errors.inc();
        close(c, 1, "invalid role");
        return;
    }
    c.hello = true;
    c.role = m.role;
    c.features = m.features & (wire::feature::datagrams | wire::feature::zstd);
    c.use_stream = !(c.features & wire::feature::datagrams);
    if (c.use_stream && c.role == wire::Role::browser) {
        auto* sc = new StreamCtx{&c, StreamKind::delta};
        if (QUIC_SUCCEEDED(api_->StreamOpen(c.h, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, &Callbacks::stream, sc, &c.delta_stream))) {
            api_->StreamStart(c.delta_stream, QUIC_STREAM_START_FLAG_IMMEDIATE);
        } else {
            delete sc;
            c.delta_stream = nullptr;
        }
    }
    if (c.role == wire::Role::host) {
        QUIC_SETTINGS s{};
        s.IsSet.KeepAliveIntervalMs = 1;
        s.KeepAliveIntervalMs = ctx_.cfg.quic.host_keepalive_ms;
        (void)api_->SetParam(c.h, QUIC_PARAM_CONN_SETTINGS, sizeof(s), &s);
        stats_.conns_host.set(stats_.conns_host.get() + 1);
    } else {
        stats_.conns_browser.set(stats_.conns_browser.get() + 1);
    }
    const auto& cfg = ctx_.cfg;
    send(c, wire::Welcome{.edge_id = cfg.edge_id,
                          .proto_minor = wire::kProtoMinor,
                          .features = c.features,
                          .server_time_ms = wall_ms(),
                          .limits = {.heartbeat_ms = cfg.registry.heartbeat_ms,
                                     .ttl_ms = cfg.registry.ttl_ms,
                                     .max_subscriptions = cfg.limits.max_subscriptions,
                                     .max_window = cfg.registry.windows.back(),
                                     .max_query_limit = cfg.registry.max_query_limit,
                                     .host_update_burst = cfg.limits.host_update_burst,
                                     .host_update_per_sec = static_cast<u32>(cfg.limits.host_update_per_sec)}});
}

void Shard::on_subscribe(Conn& c, const wire::Subscribe& m) {
    if (c.subs.size() >= std::min<std::size_t>(ctx_.cfg.limits.max_subscriptions, c.subs.capacity()))
        return send_error(c, m.req_id, wire::ErrorCode::limit_exceeded, "too many subscriptions");
    for (const Sub& s : c.subs)
        if (s.sub_id == m.sub_id) return send_error(c, m.req_id, wire::ErrorCode::bad_request, "duplicate sub_id");
    c.subs.push_back(Sub{.sub_id = m.sub_id, .spec = m.view, .window = m.window});
    ctx_.replica.post(index_, packed(c), SubscribeReq{.req_id = m.req_id, .sub_id = m.sub_id, .spec = m.view, .window = m.window});
}

void Shard::on_unsubscribe(Conn& c, const wire::Unsubscribe& m) {
    for (std::size_t i = 0; i < c.subs.size(); ++i) {
        Sub s = c.subs[i];
        if (s.sub_id != m.sub_id) continue;
        if (s.view_id) {
            auto& vec = view_subs_[s.view_id];
            const SlotHandle moved = vec.back();
            vec[s.pos] = moved;
            vec.pop_back();
            if (s.pos < vec.size())
                if (Conn* o = conn_of(moved.pack()))
                    if (Sub* os = find_sub_by_view(*o, s.view_id)) os->pos = s.pos;
            c.dirty.erase_view(s.view_id);
            ctx_.replica.post(index_, packed(c), UnsubscribeReq{.view_id = s.view_id});
        }
        c.subs[i] = c.subs.back();
        c.subs.pop_back();
        return;
    }
}

void Shard::on_query(Conn& c, wire::Query& m) {
    if (!c.query_rate.take(now_ms_)) {
        stats_.rate_limited.inc();
        return send_error(c, m.req_id, wire::ErrorCode::rate_limited, "too many queries", static_cast<u32>(c.query_rate.wait_ms(now_ms_)));
    }
    if (!m.text.empty() && !ctx_.search.empty()) {
        if (!registry::valid_text(m.text, 64, false)) return send_error(c, m.req_id, wire::ErrorCode::bad_request, "invalid text");
        auto* q = new TextQueryReq();
        q->shard = index_;
        q->conn = packed(c);
        q->req_id = m.req_id;
        q->spec = m.view;
        q->text = std::move(m.text);
        q->limit = m.limit;
        q->cursor = std::move(m.cursor);
        const u64 n = ctx_.next_search.fetch_add(1, std::memory_order_relaxed);
        ctx_.search[n % ctx_.search.size()]->post(q);
        return;
    }
    ctx_.replica.post(index_, packed(c), QueryReq{.req_id = m.req_id, .spec = m.view, .limit = m.limit, .cursor = std::move(m.cursor)});
}

void Shard::on_resolve(Conn& c, const wire::Resolve& m) {
    if (!c.query_rate.take(now_ms_)) {
        stats_.rate_limited.inc();
        return send_error(c, m.req_id, wire::ErrorCode::rate_limited, "too many requests", static_cast<u32>(c.query_rate.wait_ms(now_ms_)));
    }
    ctx_.replica.post(index_, packed(c), ResolveReq{.req_id = m.req_id, .id = m.id});
}

void Shard::on_join(Conn& c, wire::Join& m) {
    const u64 key = hash_bytes(c.addr.bytes.data(), 16, UuidHash{}(m.id));
    if (!c.query_rate.take(now_ms_) || !ctx_.join_rate.take(key, now_ms_)) {
        stats_.rate_limited.inc();
        return send_error(c, m.req_id, wire::ErrorCode::rate_limited, "too many join attempts", 12'000);
    }
    JoinReq r{.req_id = m.req_id, .id = m.id};
    if (m.password && !m.password->empty())
        r.password_mac = security::hmac_sha256(ctx_.pepper, {std::span<const u8>(m.id.bytes), security::as_bytes(*m.password)});
    ctx_.replica.post(index_, packed(c), std::move(r));
}

void Shard::on_host_register(Conn& c, wire::HostRegister& m) {
    if (c.role != wire::Role::host) return send_error(c, m.req_id, wire::ErrorCode::unauthorized, "hello as host first");
    if (c.host_handle || c.host_pending) return send_error(c, m.req_id, wire::ErrorCode::conflict, "already registered on this connection");
    if (auto err = registry::validate_register(m, ctx_.cfg.registry.limits))
        return send_error(c, m.req_id, wire::ErrorCode::bad_request, std::string("invalid ") + err->field);
    HostRegisterReq r;
    if (m.password && !m.password->empty())
        r.password_mac = security::hmac_sha256(ctx_.pepper, {std::span<const u8>(m.id.bytes), security::as_bytes(*m.password)});
    m.password.reset();
    if (m.token) r.presented_token_hash = security::sha256(*m.token);
    m.token.reset();
    security::random_bytes(r.new_token);
    r.new_token_hash = security::sha256(r.new_token);
    r.addr = c.addr;
    r.region = ctx_.geoip ? ctx_.geoip->lookup(c.addr) : wire::Region::all;
    c.host_id = m.id;
    r.msg = std::move(m);
    c.host_pending = true;
    ctx_.replica.post(index_, packed(c), std::move(r));
}

void Shard::on_host_update(Conn& c, wire::HostUpdate& m) {
    if (!c.host_handle) return send_error(c, m.req_id, wire::ErrorCode::unauthorized, "not registered");
    if (auto err = registry::validate_update(m, ctx_.cfg.registry.limits))
        return send_error(c, m.req_id, wire::ErrorCode::bad_request, std::string("invalid ") + err->field);
    std::optional<std::optional<security::Digest>> pwd;
    if (m.password) {
        if (m.password->empty()) {
            pwd.emplace(std::nullopt);
        } else {
            // The MAC binds the password to the entry id.
            pwd.emplace(security::hmac_sha256(ctx_.pepper, {std::span<const u8>(c.host_id.bytes), security::as_bytes(*m.password)}));
        }
    }
    m.password.reset();
    if (c.update_rate.take(now_ms_)) return send_host_update(c, m, std::move(pwd));
    if (m.req_id) {
        stats_.rate_limited.inc();
        return send_error(c, m.req_id, wire::ErrorCode::rate_limited, "too many updates", static_cast<u32>(c.update_rate.wait_ms(now_ms_)));
    }
    // Fire-and-forget updates coalesce into one latest-value-wins update.
    if (!c.deferred_update) c.deferred_update.emplace();
    auto& d = *c.deferred_update;
    if (m.name) d.name = std::move(m.name);
    if (m.description) d.description = std::move(m.description);
    if (m.version) d.version = std::move(m.version);
    if (m.author) d.author = std::move(m.author);
    if (m.game_port) d.game_port = m.game_port;
    if (m.max_players) d.max_players = m.max_players;
    if (m.hidden) d.hidden = m.hidden;
    if (m.players) d.players = m.players;
    if (pwd) c.deferred_password = std::move(pwd);
    if (!c.update_timer.armed()) wheel_.schedule(&c.update_timer, now_ms_ + c.update_rate.wait_ms(now_ms_));
}

void Shard::send_host_update(Conn& c, wire::HostUpdate& m, std::optional<std::optional<security::Digest>> pwd) {
    ctx_.replica.post(index_, packed(c), HostUpdateReq{.req_id = m.req_id, .handle = c.host_handle, .msg = std::move(m), .password_mac = std::move(pwd)});
}

void Shard::on_host_unregister(Conn& c, const wire::HostUnregister& m) {
    if (!c.host_handle) return send_error(c, m.req_id, wire::ErrorCode::unauthorized, "not registered");
    ctx_.replica.post(index_, packed(c), HostUnregisterReq{.req_id = m.req_id, .handle = c.host_handle});
    wheel_.cancel(&c.ttl);
    c.host_handle = 0;
}

// ---- replies ---------------------------------------------------------------------------------

void Shard::handle_reply(u64 conn, const ReplyPayload& payload) {
    Conn* cp = conn_of(conn);
    if (!cp || cp->closing) {
        // Keep subscriber counts exact even if the connection vanished meanwhile.
        if (auto* sr = std::get_if<SubscribeReply>(&payload); sr && !sr->resync)
            ctx_.replica.post(index_, conn, UnsubscribeReq{.view_id = sr->view_id});
        return;
    }
    Conn& c = *cp;
    std::visit(
        [&](const auto& r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, SubscribeReply>) {
                Sub* s = nullptr;
                for (Sub& x : c.subs)
                    if (x.sub_id == r.sub_id) s = &x;
                if (!s) {
                    if (!r.resync) ctx_.replica.post(index_, conn, UnsubscribeReq{.view_id = r.view_id});
                    return;
                }
                if (s->view_id && s->view_id == r.view_id) {  // resync
                    s->resyncing = false;
                    s->dirty = 0;
                    c.dirty.erase_view(r.view_id);
                    send_snapshot(c, r.snapshot);
                    return;
                }
                s->view_id = r.view_id;
                s->window = r.window;
                if (r.view_id >= view_subs_.size()) view_subs_.resize(r.view_id + 64);
                if (r.view_id >= view_vseq_.size()) view_vseq_.resize(r.view_id + 64, 0);
                view_vseq_[r.view_id] = std::max(view_vseq_[r.view_id], r.vseq);
                s->pos = static_cast<u32>(view_subs_[r.view_id].size());
                view_subs_[r.view_id].push_back(c.self);
                send(c, wire::SubOpen{.req_id = r.req_id, .sub_id = r.sub_id, .view_id = r.view_id, .window = r.window});
                send_snapshot(c, r.snapshot);
            } else if constexpr (std::is_same_v<T, JoinReply>) {
                const u64 expires = wall_ms() + 60'000;
                u8 tail[10];
                tail[0] = static_cast<u8>(r.port >> 8);
                tail[1] = static_cast<u8>(r.port);
                for (int i = 0; i < 8; ++i) tail[2 + i] = static_cast<u8>(expires >> (56 - 8 * i));
                const auto mac = security::hmac_sha256(ctx_.ticket_key, {std::span<const u8>(r.id.bytes), std::span<const u8>(r.addr.bytes), std::span<const u8>(tail)});
                wire::JoinGrant g{.req_id = r.req_id, .port = r.port, .ticket = wire::Bytes(mac.begin(), mac.begin() + 16), .expires_ms = expires};
                if (r.addr.is_v4()) g.address.assign(r.addr.bytes.begin() + 12, r.addr.bytes.end());
                else g.address.assign(r.addr.bytes.begin(), r.addr.bytes.end());
                send(c, g);
            } else if constexpr (std::is_same_v<T, HostRegisteredReply>) {
                c.host_pending = false;
                c.host_handle = r.handle;
                if (const PubEntry* p = ctx_.replica.pub(r.handle)) c.host_id = p->id;
                wheel_.schedule(&c.ttl, now_ms_ + ctx_.cfg.registry.ttl_ms);
                send(c, r.msg);
            } else if constexpr (std::is_same_v<T, HostSupersededReply>) {
                c.host_handle = 0;
                wheel_.cancel(&c.ttl);
                send_error(c, 0, wire::ErrorCode::conflict, "entry taken over by another connection");
                close(c, 2, "superseded");
            } else if constexpr (std::is_same_v<T, wire::Error>) {
                if (c.host_pending && !c.host_handle) c.host_pending = false;
                send(c, r);
            } else {
                send(c, r);  // QueryResult, ResolveResult, Ack, HostStatus
            }
        },
        payload);
}

}  // namespace sb::edge
