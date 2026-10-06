#include "registry/replica.hpp"

#include <zstd.h>

#include <algorithm>
#include <chrono>

#include "core/time.hpp"
#include "wire/frame.hpp"

namespace sb::registry {

std::string_view KeyLess::name_of(u32 handle) const noexcept { return rep->folded_name(handle); }

namespace {

constexpr u64 kOwnerTtlMs = 30ull * 24 * 3600 * 1000;
constexpr u32 kProbeFailuresToHide = 3;

constexpr u64 view_key(u64 pkey, wire::Sort sort, u32 window) noexcept {
    return (pkey << 16) | (u64{static_cast<u8>(sort)} << 12) | (window & 0xFFF);
}

bool valid_spec(const wire::ViewSpec& s) noexcept {
    return static_cast<u32>(s.password) <= 2 && static_cast<u32>(s.region) < wire::kRegionCount &&
           static_cast<u32>(s.sort) <= 2;
}

bool contains(const u64* a, u8 n, u64 v) noexcept { return std::find(a, a + n, v) != a + n; }

void sorted_insert(std::vector<u32>& v, u32 x) {
    auto it = std::lower_bound(v.begin(), v.end(), x);
    if (it == v.end() || *it != x) v.insert(it, x);
}

void sorted_erase(std::vector<u32>& v, u32 x) {
    auto it = std::lower_bound(v.begin(), v.end(), x);
    if (it != v.end() && *it == x) v.erase(it);
}

wire::Bytes ip_bytes(const IpAddr& a) { return {a.bytes.begin(), a.bytes.end()}; }

IpAddr ip_from(const wire::Bytes& b) {
    IpAddr a;
    if (b.size() == 16) std::copy(b.begin(), b.end(), a.bytes.begin());
    return a;
}

// Cursor: u8 version | u8 sort | u64 primary (LE) | u32 handle (LE) | folded name (name sort only).
wire::Bytes encode_cursor(wire::Sort sort, const SortKey& k, std::string_view folded) {
    wire::Bytes b;
    b.reserve(14 + folded.size());
    b.push_back(1);
    b.push_back(static_cast<u8>(sort));
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<u8>(k.primary >> (8 * i)));
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>(k.handle >> (8 * i)));
    if (sort == wire::Sort::name) b.insert(b.end(), folded.begin(), folded.end());
    return b;
}

bool decode_cursor(const wire::Bytes& b, wire::Sort sort, CursorKey& out) {
    if (b.size() < 14 || b[0] != 1 || b[1] != static_cast<u8>(sort)) return false;
    out.primary = 0;
    for (int i = 0; i < 8; ++i) out.primary |= u64{b[2 + i]} << (8 * i);
    out.handle = 0;
    for (int i = 0; i < 4; ++i) out.handle |= u32{b[10 + i]} << (8 * i);
    if (sort == wire::Sort::name) {
        if (b.size() - 14 > 256) return false;
        out.folded_name = std::string_view(reinterpret_cast<const char*>(b.data() + 14), b.size() - 14);
    } else if (b.size() != 14) {
        return false;
    }
    return true;
}

}  // namespace

Replica::Replica(ReplicaConfig cfg, backbone::Backbone& backbone)
    : cfg_(std::move(cfg)),
      backbone_(backbone),
      ring_(cfg_.ring_capacity),
      pub_table_(std::make_unique<std::atomic<const PubEntry*>[]>(cfg_.max_entries)) {
    SB_ASSERT(cfg_.max_entries > 1);
    SB_ASSERT(!cfg_.windows.empty());
    std::sort(cfg_.windows.begin(), cfg_.windows.end());
    records_.resize(1);  // handle 0 is never issued
    timers_.resize(1);
    views_.emplace_back();  // view id 0 is never issued
    backbone_.start([this](ReplicaMsg* m) { post(m); });
}

Replica::~Replica() {
    while (MpscNode* n = inbox_.pop()) delete static_cast<ReplicaMsg*>(n);
    for (auto& [seq, ev] : in_flight_) {
        for (const PubEntry* p : ev->retired) delete p;
        delete ev;
    }
    for (u32 h = 0; h < cfg_.max_entries; ++h) delete pub_table_[h].load(std::memory_order_relaxed);
}

// ---- event loop ------------------------------------------------------------------------------

void Replica::run(std::stop_token stop) {
    std::stop_callback wake_on_stop(stop, [this] { waker_.wake(); });
    while (!stop.stop_requested()) {
        const u64 now = mono_ms();
        if (poll_once(now)) continue;
        waker_.announce_sleep();
        if (!inbox_.empty() || stop.stop_requested()) {
            waker_.cancel_sleep();
            continue;
        }
        const u64 wait = std::min<u64>(wheel_.ms_to_next_tick(now), in_flight_.empty() ? 1000 : 10);
        waker_.block(std::chrono::milliseconds(wait));
    }
}

bool Replica::poll_once(u64 now_ms) {
    if (!wheel_started_) {
        wheel_.start(now_ms);
        wheel_started_ = true;
    }
    bool did = false;
    for (int budget = 4096; budget > 0; --budget) {
        MpscNode* n = inbox_.pop();
        if (!n) break;
        auto* m = static_cast<ReplicaMsg*>(n);
        handle(*m, now_ms);
        delete m;
        did = true;
    }
    wheel_.advance(now_ms, [&](TimerNode* t) { fire_timer(t, now_ms); });
    reclaim();
    return did;
}

void Replica::handle(ReplicaMsg& m, u64 now_ms) {
    std::visit([&](auto& p) { on(m.shard, m.conn, p, now_ms); }, m.payload);
}

void Replica::publish_event(RingEvent* ev) {
    const u64 seq = ring_.publish(ev, [this] { stats_.ring_stalls.fetch_add(1, std::memory_order_relaxed); });
    in_flight_.emplace_back(seq, ev);
    ring_.wake_all();
}

void Replica::publish_drain(u32 phase) {
    auto* ev = new RingEvent();
    ev->kind = RingEvent::Kind::drain;
    ev->conn = phase;
    publish_event(ev);
}

void Replica::on(u16, u64, DrainReq& r, u64) { publish_drain(r.phase); }

void Replica::reclaim() {
    if (in_flight_.empty()) return;
    const u64 min = ring_.min_cursor();
    while (!in_flight_.empty() && in_flight_.front().first < min) {
        RingEvent* ev = in_flight_.front().second;
        for (const PubEntry* p : ev->retired) delete p;
        delete ev;
        in_flight_.pop_front();
    }
}

void Replica::reply(u16 shard, u64 conn, ReplyPayload p) {
    if (shard == kNoShard) return;
    auto* ev = new RingEvent();
    ev->kind = RingEvent::Kind::reply;
    ev->target = shard;
    ev->conn = conn;
    ev->reply.emplace(std::move(p));
    publish_event(ev);
}

void Replica::reply_error(u16 shard, u64 conn, u32 req_id, wire::ErrorCode code, std::string msg, u32 retry_ms) {
    reply(shard, conn, wire::Error{.req_id = req_id, .code = code, .message = std::move(msg), .retry_after_ms = retry_ms});
}

// ---- records ---------------------------------------------------------------------------------

const Record* Replica::find(const Uuid& id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : records_[it->second].get();
}

Record* Replica::owned_by(u32 handle, u16 shard, u64 conn) noexcept {
    Record* r = by_handle(handle);
    if (!r || r->owner_edge != cfg_.edge_id || r->shard != shard || r->conn != conn) return nullptr;
    return r;
}

Record& Replica::create_record(const Uuid& id) {
    u32 h;
    if (!free_handles_.empty()) {
        h = free_handles_.front();
        free_handles_.pop_front();
    } else {
        SB_ASSERT(next_handle_ < cfg_.max_entries);
        h = next_handle_++;
        records_.resize(h + 1);
        timers_.resize(h + 1);
    }
    records_[h] = std::make_unique<Record>();
    timers_[h] = std::make_unique<RecordTimers>();
    timers_[h]->grace.cookie = timers_[h]->persist.cookie = timers_[h]->probe.cookie = h;
    Record& r = *records_[h];
    r.id = id;
    r.handle = h;
    by_id_.emplace(id, h);
    stats_.entries.store(by_id_.size(), std::memory_order_relaxed);
    return r;
}

void Replica::remove_record(Record& r, u64 now_ms) {
    const u32 h = r.handle;
    mutate(r, now_ms, [](Record&) {}, true);
    ip_count(r.addr, -1);
    wheel_.cancel(&timers_[h]->grace);
    wheel_.cancel(&timers_[h]->persist);
    wheel_.cancel(&timers_[h]->probe);
    by_id_.erase(r.id);
    records_[h].reset();
    timers_[h].reset();
    free_handles_.push_back(h);
    stats_.entries.store(by_id_.size(), std::memory_order_relaxed);
}

void Replica::ip_count(const IpAddr& a, int delta) {
    if (a == IpAddr{}) return;
    auto& n = hosts_per_ip_[a];
    if (delta < 0 && n > 0) --n;
    else if (delta > 0) ++n;
    if (n == 0) hosts_per_ip_.erase(a);
}

// ---- partitions and views --------------------------------------------------------------------

Partition& Replica::partition(u64 key) {
    auto it = partitions_.find(key);
    if (it != partitions_.end()) return *it->second;
    return *partitions_.emplace(key, std::make_unique<Partition>(this, key)).first->second;
}

View& Replica::view_for(const wire::ViewSpec& spec, u32 window) {
    const u64 pk = partition_of(spec);
    const u64 vk = view_key(pk, spec.sort, window);
    if (auto it = view_ids_.find(vk); it != view_ids_.end()) return *views_[it->second];
    auto v = std::make_unique<View>();
    v->id = static_cast<u32>(views_.size());
    v->pkey = pk;
    v->sort = spec.sort;
    v->window = window;
    v->vseq = 1;
    v->part = &partition(pk);
    v->part->views[static_cast<int>(spec.sort)].push_back(v->id);
    view_ids_.emplace(vk, v->id);
    views_.push_back(std::move(v));
    stats_.views.store(views_.size() - 1, std::memory_order_relaxed);
    attach_initial_members(*views_.back());
    return *views_.back();
}

void Replica::attach_initial_members(View& v) {
    SortTree& t = v.part->trees[static_cast<int>(v.sort)];
    if (t.empty()) return;
    auto* ev = new RingEvent();
    u32 n = 0;
    for (auto it = t.begin(); it.valid() && n < v.window; ++it, ++n) {
        Record& r = *records_[it->handle];
        sorted_insert(r.views, v.id);
        republish(r, *ev, false);
    }
    publish_event(ev);
}

void Replica::republish(Record& r, RingEvent& ev, bool) {
    auto* p = new PubEntry();
    p->handle = r.handle;
    p->id = r.id;
    p->name = r.name;
    p->author = r.author;
    p->version = r.version;
    p->bucket = r.bucket;
    p->players = r.players;
    p->max_players = r.max_players;
    p->flags = r.flags();
    p->region = r.region;
    p->created_ms = r.created_ms;
    p->views = r.views;
    if (r.pub) ev.retired.push_back(r.pub);
    r.pub = p;
    pub_table_[r.handle].store(p, std::memory_order_release);
    if (ev.handle == r.handle) ev.pub = p;
}

std::shared_ptr<const SnapshotBlob> Replica::snapshot_of(View& v) {
    if (v.snapshot && v.snapshot->vseq == v.vseq) return v.snapshot;
    SortTree& t = v.part->trees[static_cast<int>(v.sort)];
    wire::Snapshot s{.view_id = v.id, .vseq = v.vseq, .total = static_cast<u32>(t.size())};
    s.entries.reserve(std::min<std::size_t>(v.window, t.size()));
    u32 n = 0;
    for (auto it = t.begin(); it.valid() && n < v.window; ++it, ++n) records_[it->handle]->to_list_entry(s.entries.emplace_back());

    auto blob = std::make_shared<SnapshotBlob>();
    blob->view_id = v.id;
    blob->vseq = v.vseq;
    wire::Writer w(64 + s.entries.size() * 96);
    wire::encode_frame(w, s);
    blob->frame = w.take();

    if (cfg_.zstd && blob->frame.size() > 1024) {
        wire::Writer raw;
        wire::encode(raw, s);
        std::vector<u8> z(ZSTD_compressBound(raw.size()));
        const std::size_t zn = ZSTD_compress(z.data(), z.size(), raw.data(), raw.size(), 3);
        if (!ZSTD_isError(zn) && zn + 16 < blob->frame.size() * 4 / 5) {
            wire::Writer zf;
            zf.quic_varint(static_cast<u64>(wire::FrameType::snapshot_zstd));
            const std::size_t at = zf.reserve_len4();
            zf.varint(raw.size());
            zf.put(z.data(), zn);
            zf.patch_len4(at);
            blob->zstd_frame = zf.take();
        }
    }
    v.snapshot = blob;
    return blob;
}

// ---- the mutation core -----------------------------------------------------------------------

namespace {

enum class Intent : u8 { partial, full, removal };

struct ViewIntent {
    u32 view_id;
    u32 handle;
    Intent kind;
    u8 mask;
};

struct Membership {
    u32 handle;
    u32 view_id;
    bool add;
};

}  // namespace

template <class F>
void Replica::mutate(Record& r, u64 now_ms, F&& change, bool deleting) {
    stats_.mutations.fetch_add(1, std::memory_order_relaxed);
    const u32 h = r.handle;

    // Old state, captured before the change.
    const bool was_indexed = r.indexed;
    u64 old_parts[8];
    const u8 old_np = r.npartitions;
    std::copy(r.partitions, r.partitions + old_np, old_parts);
    const SortKey old_keys[3] = {r.keys[0], r.keys[1], r.keys[2]};
    const u32 old_players = r.players, old_max = r.max_players, old_flags = r.flags();
    const std::string old_name = r.name;
    const std::string old_author = r.author;
    const std::string old_version = r.version;
    const wire::Region old_region = r.region;
    const bool was_published = r.pub != nullptr;

    change(r);
    if (!deleting) r.updated_ms = std::max(r.updated_ms, now_ms);

    const bool vis = !deleting && r.visible();
    u64 new_parts[8];
    const u8 new_np = vis ? partitions_of(r.bucket, r.password_mac.has_value(), r.region, new_parts) : 0;
    std::string new_folded = fold(r.name);
    const SortKey new_keys[3] = {{players_primary(r.players, r.created_ms), h},
                                 {newest_primary(r.created_ms), h},
                                 {name_primary(new_folded), h}};
    const bool folded_same = new_folded == r.folded_name;

    u8 mask = 0;
    if (r.players != old_players || r.max_players != old_max) mask |= patch_mask::players;
    if (r.flags() != old_flags) mask |= patch_mask::flags;
    if (r.name != old_name) mask |= patch_mask::name;
    if (r.author != old_author || r.version != old_version || r.region != old_region) mask |= patch_mask::full;

    // Union of affected partitions.
    u64 parts[16];
    u8 np = 0;
    if (was_indexed)
        for (u8 i = 0; i < old_np; ++i) parts[np++] = old_parts[i];
    for (u8 i = 0; i < new_np; ++i)
        if (!contains(parts, np, new_parts[i])) parts[np++] = new_parts[i];

    struct TreeState {
        Partition* p;
        int sort;
        bool in_old, in_new, same;
        std::size_t r_old;
    };
    inplace_vector<TreeState, 48> trees;

    // Phase 1: old ranks and erasure, while the indexes still see the old folded name.
    for (u8 i = 0; i < np; ++i) {
        Partition& P = partition(parts[i]);
        const bool in_old = was_indexed && contains(old_parts, old_np, parts[i]);
        const bool in_new = vis && contains(new_parts, new_np, parts[i]);
        for (int s = 0; s < 3; ++s) {
            const bool same = in_old && in_new && old_keys[s].primary == new_keys[s].primary &&
                              (s != 2 || folded_same);
            TreeState ts{&P, s, in_old, in_new, same, 0};
            SortTree& T = P.trees[s];
            if (in_old && !P.views[s].empty()) ts.r_old = T.rank(old_keys[s]);
            if (in_old && !same) T.erase(old_keys[s]);
            trees.push_back(ts);
        }
    }

    // Phase 2: switch the record to its new index identity.
    r.folded_name = std::move(new_folded);
    std::copy(new_keys, new_keys + 3, r.keys);
    std::copy(new_parts, new_parts + new_np, r.partitions);
    r.npartitions = new_np;
    r.indexed = vis;

    // Phase 3: insertion, new ranks and window transitions.
    std::vector<ViewIntent> intents;
    std::vector<Membership> members;
    for (const TreeState& ts : trees) {
        SortTree& T = ts.p->trees[ts.sort];
        if (ts.in_new && !ts.same) T.insert(new_keys[ts.sort]);
        const auto& vlist = ts.p->views[ts.sort];
        if (vlist.empty()) continue;
        const std::size_t r_new = ts.in_new ? T.rank(new_keys[ts.sort]) : 0;
        for (u32 vid : vlist) {
            const u32 K = views_[vid]->window;
            const bool was_in = ts.in_old && ts.r_old < K;
            const bool is_in = ts.in_new && r_new < K;
            if (was_in && is_in) {
                if (mask) intents.push_back({vid, h, (mask & patch_mask::full) ? Intent::full : Intent::partial, mask});
            } else if (!was_in && is_in) {
                intents.push_back({vid, h, Intent::full, patch_mask::full});
                members.push_back({h, vid, true});
                if (T.size() > K) {
                    const u32 out = T.select(K).handle;
                    intents.push_back({vid, out, Intent::removal, patch_mask::full});
                    members.push_back({out, vid, false});
                }
            } else if (was_in && !is_in) {
                intents.push_back({vid, h, Intent::removal, patch_mask::full});
                members.push_back({h, vid, false});
                if (T.size() >= K) {
                    const u32 in = T.select(K - 1).handle;
                    intents.push_back({vid, in, Intent::full, patch_mask::full});
                    members.push_back({in, vid, true});
                }
            }
        }
    }

    // Window memberships, then the new immutable entries.
    auto* ev = new RingEvent();
    ev->handle = h;
    ev->text_changed = !was_published || deleting || r.name != old_name || r.author != old_author;
    std::vector<u32> touched;
    for (const Membership& m : members) {
        Record* x = records_[m.handle].get();
        if (m.add) sorted_insert(x->views, m.view_id);
        else sorted_erase(x->views, m.view_id);
        if (m.handle != h && std::find(touched.begin(), touched.end(), m.handle) == touched.end())
            touched.push_back(m.handle);
    }
    for (u32 nh : touched) republish(*records_[nh], *ev, false);
    if (deleting) {
        if (r.pub) ev->retired.push_back(r.pub);
        r.pub = nullptr;
        pub_table_[h].store(nullptr, std::memory_order_release);
        ev->pub = nullptr;
    } else {
        republish(r, *ev, true);
    }

    // One frame per affected view: all patches of this mutation share the view's next vseq.
    const u64 now_ns = cfg_.num_shards > 0 ? mono_ns() : 0;
    std::sort(intents.begin(), intents.end(), [](const ViewIntent& a, const ViewIntent& b) { return a.view_id < b.view_id; });
    wire::Writer w(256);
    for (std::size_t i = 0; i < intents.size();) {
        std::size_t j = i;
        while (j < intents.size() && intents[j].view_id == intents[i].view_id) ++j;
        View& v = *views_[intents[i].view_id];
        const u64 vseq = ++v.vseq;
        if (v.subscribers > 0 && cfg_.num_shards > 0) {
            wire::Delta d{.view_id = v.id};
            inplace_vector<FrameItem, 2> items;
            for (std::size_t k = i; k < j; ++k) {
                const ViewIntent& in = intents[k];
                wire::Patch& p = d.patches.emplace_back();
                p.handle = in.handle;
                p.vseq = vseq;
                const Record& x = *records_[in.handle];
                switch (in.kind) {
                    case Intent::removal: p.removed = true; break;
                    case Intent::full: x.to_list_entry(p.entry.emplace()); break;
                    case Intent::partial:
                        if (in.mask & patch_mask::players) {
                            p.players = x.players;
                            p.max_players = x.max_players;
                        }
                        if (in.mask & patch_mask::flags) p.flags = x.flags();
                        if (in.mask & patch_mask::name) p.name = x.name;
                        break;
                }
                if (items.size() < items.capacity()) items.push_back({in.handle, in.mask});
            }
            w.clear();
            wire::encode_frame(w, d, wire::LenWidth::two);
            Frame* f = Frame::make(w.view());
            f->view_id = v.id;
            f->vseq = vseq;
            f->created_ns = now_ns;
            f->items = items;
            f->shard_refs.store(cfg_.num_shards, std::memory_order_relaxed);
            ev->frames.push_back(f);
            stats_.frames.fetch_add(1, std::memory_order_relaxed);
        }
        i = j;
    }
    publish_event(ev);
}

// ---- browser requests ------------------------------------------------------------------------

void Replica::on(u16 shard, u64 conn, SubscribeReq& r, u64) {
    if (!valid_spec(r.spec)) return reply_error(shard, conn, r.req_id, wire::ErrorCode::bad_request, "invalid view");
    u32 window = cfg_.windows.back();
    for (u32 w : cfg_.windows)
        if (w >= r.window) {
            window = w;
            break;
        }
    View& v = view_for(r.spec, window);
    if (!r.resync) ++v.subscribers;
    reply(shard, conn,
          SubscribeReply{.req_id = r.req_id,
                         .sub_id = r.sub_id,
                         .view_id = v.id,
                         .window = window,
                         .vseq = v.vseq,
                         .resync = r.resync,
                         .snapshot = snapshot_of(v)});
}

void Replica::on(u16, u64, UnsubscribeReq& r, u64) {
    if (r.view_id > 0 && r.view_id < views_.size() && views_[r.view_id]->subscribers > 0)
        --views_[r.view_id]->subscribers;
}

void Replica::on(u16 shard, u64 conn, QueryReq& r, u64) {
    if (!valid_spec(r.spec)) return reply_error(shard, conn, r.req_id, wire::ErrorCode::bad_request, "invalid view");
    const u32 limit = r.limit == 0 ? 50 : std::min(r.limit, cfg_.max_query_limit);
    wire::QueryResult res{.req_id = r.req_id};
    auto pit = partitions_.find(partition_of(r.spec));
    if (pit == partitions_.end()) return reply(shard, conn, std::move(res));
    SortTree& t = pit->second->trees[static_cast<int>(r.spec.sort)];
    res.total = static_cast<u32>(t.size());
    SortTree::Iterator it;
    if (r.cursor.empty()) {
        it = t.begin();
    } else {
        CursorKey ck;
        if (!decode_cursor(r.cursor, r.spec.sort, ck))
            return reply_error(shard, conn, r.req_id, wire::ErrorCode::bad_request, "invalid cursor");
        it = t.upper_bound(ck);
    }
    res.entries.reserve(limit);
    SortKey last{};
    for (; it.valid() && res.entries.size() < limit; ++it) {
        records_[it->handle]->to_list_entry(res.entries.emplace_back());
        last = *it;
    }
    if (it.valid() && !res.entries.empty())
        res.next_cursor = encode_cursor(r.spec.sort, last, records_[last.handle]->folded_name);
    reply(shard, conn, std::move(res));
}

void Replica::on(u16 shard, u64 conn, ResolveReq& r, u64) {
    wire::ResolveResult res{.req_id = r.req_id};
    if (const Record* rec = find(r.id)) {
        auto& d = res.details.emplace();
        rec->to_list_entry(d.entry);
        d.description = rec->description;
        d.updated_ms = rec->updated_ms;
    }
    reply(shard, conn, std::move(res));
}

void Replica::on(u16 shard, u64 conn, JoinReq& r, u64) {
    const Record* rec = find(r.id);
    if (!rec) return reply_error(shard, conn, r.req_id, wire::ErrorCode::not_found, "no such server");
    if (!rec->online) return reply_error(shard, conn, r.req_id, wire::ErrorCode::unavailable, "server offline");
    if (!rec->reachable) return reply_error(shard, conn, r.req_id, wire::ErrorCode::unreachable, "server unreachable");
    if (rec->password_mac &&
        (!r.password_mac || !security::equal_ct(*r.password_mac, *rec->password_mac)))
        return reply_error(shard, conn, r.req_id, wire::ErrorCode::wrong_password, "wrong password");
    reply(shard, conn, JoinReply{.req_id = r.req_id, .id = rec->id, .addr = rec->addr, .port = rec->port});
}

// ---- host lifecycle --------------------------------------------------------------------------

backbone::ReplicatedRecord Replica::to_replicated(const Record& r) const {
    backbone::ReplicatedRecord rr;
    rr.id = r.id;
    rr.name = r.name;
    rr.description = r.description;
    rr.version = r.version;
    rr.author = r.author;
    rr.players = r.players;
    rr.max_players = r.max_players;
    rr.region = r.region;
    rr.hidden = r.hidden;
    rr.reachable = r.reachable;
    rr.online = r.online;
    rr.created_ms = r.created_ms;
    rr.updated_ms = r.updated_ms;
    rr.addr = ip_bytes(r.addr);
    rr.port = r.port;
    if (r.password_mac) rr.password_mac.assign(r.password_mac->begin(), r.password_mac->end());
    rr.token_hash.assign(r.token_hash.begin(), r.token_hash.end());
    rr.owner_edge = r.owner_edge;
    rr.ver = r.ver;
    return rr;
}

void Replica::apply_replicated(Record& r, const backbone::ReplicatedRecord& rr, bool soft_too) {
    // Lifecycle fields: authoritative in stream order.
    if (r.addr != ip_from(rr.addr)) {
        ip_count(r.addr, -1);
        r.addr = ip_from(rr.addr);
        ip_count(r.addr, +1);
    }
    r.port = static_cast<u16>(rr.port);
    r.version = rr.version;
    r.bucket = version_bucket(rr.version);
    r.author = rr.author;
    r.region = rr.region;
    r.created_ms = rr.created_ms;
    if (rr.password_mac.size() == 32) {
        Digest d;
        std::copy(rr.password_mac.begin(), rr.password_mac.end(), d.begin());
        r.password_mac = d;
    } else {
        r.password_mac.reset();
    }
    if (rr.token_hash.size() == 32) std::copy(rr.token_hash.begin(), rr.token_hash.end(), r.token_hash.begin());
    r.owner_edge = rr.owner_edge;
    // Soft fields: only if not older than what this edge already applied.
    if (soft_too) {
        r.name = rr.name;
        r.description = rr.description;
        r.players = rr.players;
        r.max_players = rr.max_players;
        r.hidden = rr.hidden;
        r.reachable = rr.reachable;
        r.online = rr.online;
        r.updated_ms = rr.updated_ms;
        r.ver = rr.ver;
    } else if ((rr.ver >> 32) > (r.ver >> 32)) {
        r.ver = rr.ver;
    }
}

void Replica::write_record(const Record& r, const backbone::ReplicatedRecord& rr, PendingOp op) {
    const u64 id = next_op_++;
    pending_.emplace(id, std::move(op));
    backbone_.publish_record(rr.id, wire::encode_to_bytes(rr), r.stream_seq, id);
}

void Replica::write_tombstone(const Record& r, PendingOp op) {
    const u64 id = next_op_++;
    op.id = r.id;
    pending_.emplace(id, std::move(op));
    backbone_.publish_record(r.id, {}, r.stream_seq, id);
}

void Replica::publish_soft(Record& r, const backbone::SoftUpdate& su, u64 now_ms) {
    if (backbone_.clustered()) backbone_.publish_soft(r.id, wire::encode_to_bytes(su));
    schedule_persist(r, now_ms);
}

void Replica::schedule_persist(Record& r, u64 now_ms) {
    TimerNode& t = timers_[r.handle]->persist;
    if (!t.armed()) wheel_.schedule(&t, now_ms + cfg_.persist_interval_ms);
}

void Replica::request_probe(Record& r, u64 now_ms) {
    if (!cfg_.probe_enabled || !probe_ || r.owner_edge != cfg_.edge_id) return;
    probe_(r.handle, r.addr, r.port);
    wheel_.schedule(&timers_[r.handle]->probe, now_ms + cfg_.probe_interval_ms);
}

void Replica::on(u16 shard, u64 conn, HostRegisterReq& r, u64 now_ms) {
    const auto& msg = r.msg;
    Record* ex = nullptr;
    if (auto it = by_id_.find(msg.id); it != by_id_.end()) ex = records_[it->second].get();

    Digest token_hash;
    bool return_token = false;
    u32 epoch = 1;
    u64 expected = 0;
    if (ex) {
        if (!r.presented_token_hash || !security::equal_ct(*r.presented_token_hash, ex->token_hash))
            return reply_error(shard, conn, msg.req_id, wire::ErrorCode::unauthorized, "token required for this id");
        token_hash = ex->token_hash;
        epoch = ex->lease_epoch() + 1;
        expected = ex->stream_seq;
    } else {
        auto oi = owners_.find(msg.id);
        if (oi != owners_.end() && oi->second.expires_ms > now_ms) {
            if (!r.presented_token_hash || !security::equal_ct(*r.presented_token_hash, oi->second.token_hash))
                return reply_error(shard, conn, msg.req_id, wire::ErrorCode::unauthorized, "token required for this id");
            token_hash = oi->second.token_hash;
            epoch = oi->second.epoch + 1;
            expected = oi->second.last_seq;
        } else {
            if (oi != owners_.end()) {
                expected = oi->second.last_seq;
                epoch = oi->second.epoch + 1;
            }
            if (r.presented_token_hash) {
                token_hash = *r.presented_token_hash;
            } else {
                token_hash = r.new_token_hash;
                return_token = true;
            }
        }
    }

    const bool same_ip = ex && ex->addr == r.addr;
    if (!same_ip) {
        auto it = hosts_per_ip_.find(r.addr);
        if (it != hosts_per_ip_.end() && it->second >= cfg_.max_hosts_per_ip)
            return reply_error(shard, conn, msg.req_id, wire::ErrorCode::limit_exceeded, "too many servers from this address");
    }

    backbone::ReplicatedRecord rr;
    rr.id = msg.id;
    rr.name = msg.name;
    rr.description = msg.description;
    rr.version = msg.version;
    rr.author = msg.author;
    rr.players = msg.players;
    rr.max_players = msg.max_players;
    rr.region = r.region;
    rr.hidden = msg.hidden;
    const bool same_endpoint = same_ip && ex->port == msg.game_port;
    rr.reachable = same_endpoint ? ex->reachable : !cfg_.probe_enabled;
    rr.online = true;
    rr.created_ms = ex ? ex->created_ms : now_ms;
    rr.updated_ms = now_ms;
    rr.addr = ip_bytes(r.addr);
    rr.port = msg.game_port;
    if (r.password_mac) rr.password_mac.assign(r.password_mac->begin(), r.password_mac->end());
    rr.token_hash.assign(token_hash.begin(), token_hash.end());
    rr.owner_edge = cfg_.edge_id;
    rr.ver = u64{epoch} << 32;

    PendingOp op{.kind = PendingOp::Kind::reg, .shard = shard, .conn = conn, .req_id = msg.req_id, .id = msg.id,
                 .return_token = return_token, .token = r.new_token};
    const u64 opid = next_op_++;
    pending_.emplace(opid, std::move(op));
    backbone_.publish_record(msg.id, wire::encode_to_bytes(rr), expected, opid);
}

void Replica::on(u16 shard, u64 conn, HostUpdateReq& r, u64 now_ms) {
    Record* rec = owned_by(r.handle, shard, conn);
    if (!rec) return reply_error(shard, conn, r.req_id, wire::ErrorCode::unauthorized, "not the owner of this entry");
    const auto& u = r.msg;
    const bool soft = u.players || u.max_players || u.name || u.description || u.hidden;
    const bool life = u.version || u.author || u.game_port || r.password_mac.has_value();

    if (soft) {
        backbone::SoftUpdate su{.id = rec->id, .ver = rec->ver + 1, .owner_edge = cfg_.edge_id, .updated_ms = now_ms};
        su.players = u.players;
        su.max_players = u.max_players;
        su.name = u.name;
        su.description = u.description;
        su.hidden = u.hidden;
        mutate(*rec, now_ms, [&](Record& x) {
            if (u.players) x.players = *u.players;
            if (u.max_players) x.max_players = *u.max_players;
            if (u.name) x.name = *u.name;
            if (u.description) x.description = *u.description;
            if (u.hidden) x.hidden = *u.hidden;
            x.ver = su.ver;
        });
        publish_soft(*rec, su, now_ms);
    }
    if (life) {
        backbone::ReplicatedRecord rr = to_replicated(*rec);
        if (u.version) rr.version = *u.version;
        if (u.author) rr.author = *u.author;
        const bool port_changed = u.game_port && *u.game_port != rec->port;
        if (u.game_port) rr.port = *u.game_port;
        if (port_changed && cfg_.probe_enabled) rr.reachable = false;
        if (r.password_mac) {
            rr.password_mac.clear();
            if (*r.password_mac) rr.password_mac.assign((*r.password_mac)->begin(), (*r.password_mac)->end());
        }
        rr.ver = rec->ver + 1;
        write_record(*rec, rr, PendingOp{.kind = PendingOp::Kind::update, .shard = shard, .conn = conn, .req_id = r.req_id, .id = rec->id});
    } else if (r.req_id) {
        reply(shard, conn, wire::Ack{.req_id = r.req_id});
    }
}

void Replica::on(u16 shard, u64 conn, HostUnregisterReq& r, u64 now_ms) {
    Record* rec = owned_by(r.handle, shard, conn);
    if (!rec) return reply_error(shard, conn, r.req_id, wire::ErrorCode::unauthorized, "not the owner of this entry");
    // Hide immediately; the tombstone follows through the lifecycle path.
    mutate(*rec, now_ms, [&](Record& x) {
        x.online = false;
        x.ver += 1;
    });
    publish_soft(*rec, backbone::SoftUpdate{.id = rec->id, .ver = rec->ver, .owner_edge = cfg_.edge_id, .updated_ms = now_ms, .online = false}, now_ms);
    rec->shard = kNoShard;
    write_tombstone(*rec, PendingOp{.kind = PendingOp::Kind::unregister, .shard = shard, .conn = conn, .req_id = r.req_id});
}

void Replica::on(u16 shard, u64 conn, HostGoneReq& r, u64 now_ms) {
    Record* rec = owned_by(r.handle, shard, conn);
    if (!rec) return;
    rec->shard = kNoShard;
    rec->conn = 0;
    if (rec->online && !r.draining) {
        mutate(*rec, now_ms, [&](Record& x) {
            x.online = false;
            x.ver += 1;
        });
        publish_soft(*rec, backbone::SoftUpdate{.id = rec->id, .ver = rec->ver, .owner_edge = cfg_.edge_id, .updated_ms = now_ms, .online = false}, now_ms);
    }
    wheel_.schedule(&timers_[rec->handle]->grace, now_ms + cfg_.grace_ms);
}

void Replica::on(u16, u64, ProbeResultReq& r, u64 now_ms) {
    Record* rec = by_handle(r.handle);
    if (!rec || rec->owner_edge != cfg_.edge_id) return;
    const bool was = rec->reachable;
    rec->probe_failures = r.reachable ? 0 : rec->probe_failures + 1;
    // Unproven endpoints become reachable on the first success; proven ones need 3 straight failures.
    const bool now_reachable = r.reachable || (was && rec->probe_failures < kProbeFailuresToHide);
    if (now_reachable != was) {
        mutate(*rec, now_ms, [&](Record& x) {
            x.reachable = now_reachable;
            x.ver += 1;
        });
        publish_soft(*rec, backbone::SoftUpdate{.id = rec->id, .ver = rec->ver, .owner_edge = cfg_.edge_id, .updated_ms = now_ms, .reachable = now_reachable}, now_ms);
    }
    if (rec->shard != kNoShard)
        reply(rec->shard, rec->conn, wire::HostStatus{.reachable = rec->reachable, .probe_failures = rec->probe_failures});
}

// ---- backbone deliveries ---------------------------------------------------------------------

void Replica::on(u16, u64, BackboneRecord& r, u64 now_ms) {
    auto it = by_id_.find(r.id);
    Record* rec = it == by_id_.end() ? nullptr : records_[it->second].get();

    if (r.payload.empty()) {
        auto& oi = owners_[r.id];
        oi.last_seq = r.stream_seq;
        oi.expires_ms = now_ms + kOwnerTtlMs;
        if (rec) {
            if (rec->shard != kNoShard) reply(rec->shard, rec->conn, HostSupersededReply{});
            oi.token_hash = rec->token_hash;
            oi.epoch = rec->lease_epoch();
            remove_record(*rec, now_ms);
        }
        complete_pending_for(r.id, r.stream_seq, now_ms);
        return;
    }

    backbone::ReplicatedRecord rr;
    if (!wire::decode(r.payload, rr) || rr.id != r.id) return;

    const bool created = rec == nullptr;
    if (created) rec = &create_record(r.id);
    const u32 new_epoch = static_cast<u32>(rr.ver >> 32);
    const bool owner_changed = !created && (rec->owner_edge != rr.owner_edge || new_epoch != rec->lease_epoch());
    const bool endpoint_changed = created || rec->port != rr.port || rec->addr != ip_from(rr.addr);
    if (owner_changed && rec->shard != kNoShard) {
        reply(rec->shard, rec->conn, HostSupersededReply{});
        rec->shard = kNoShard;
        rec->conn = 0;
    }
    const bool soft_too = created || owner_changed || rr.ver >= rec->ver;
    mutate(*rec, now_ms, [&](Record& x) {
        apply_replicated(x, rr, soft_too);
        x.stream_seq = r.stream_seq;
    });
    owners_[r.id] = OwnerInfo{.token_hash = rec->token_hash, .last_seq = r.stream_seq,
                              .expires_ms = now_ms + kOwnerTtlMs, .epoch = new_epoch};
    if (rec->owner_edge == cfg_.edge_id) {
        if (rec->online) wheel_.cancel(&timers_[rec->handle]->grace);  // re-registered within the grace period
        if (endpoint_changed) request_probe(*rec, now_ms);
    }
    if (created) {
        if (auto e = early_soft_.find(r.id); e != early_soft_.end()) {
            const backbone::SoftUpdate su = e->second;
            early_soft_.erase(e);
            apply_soft(*rec, su, now_ms);
        }
    }
    complete_pending_for(r.id, r.stream_seq, now_ms);
}

void Replica::on(u16, u64, BackboneSoft& r, u64 now_ms) {
    backbone::SoftUpdate su;
    if (!wire::decode(r.payload, su)) return;
    auto it = by_id_.find(su.id);
    if (it == by_id_.end()) {
        // Possibly ahead of its record during a replay: keep the newest one, bounded.
        auto [e, inserted] = early_soft_.try_emplace(su.id, su);
        if (!inserted && su.ver > e->second.ver) e->second = su;
        if (inserted) {
            early_order_.push_back(su.id);
            if (early_order_.size() > 100'000) {
                early_soft_.erase(early_order_.front());
                early_order_.pop_front();
            }
        }
        return;
    }
    apply_soft(*records_[it->second], su, now_ms);
}

void Replica::apply_soft(Record& rec, const backbone::SoftUpdate& su, u64 now_ms) {
    if (su.ver <= rec.ver || su.owner_edge != rec.owner_edge) return;
    mutate(rec, now_ms, [&](Record& x) {
        if (su.players) x.players = *su.players;
        if (su.max_players) x.max_players = *su.max_players;
        if (su.name) x.name = *su.name;
        if (su.description) x.description = *su.description;
        if (su.hidden) x.hidden = *su.hidden;
        if (su.reachable) x.reachable = *su.reachable;
        if (su.online) x.online = *su.online;
        x.updated_ms = su.updated_ms;
        x.ver = su.ver;
    });
}

void Replica::on(u16, u64, BackboneAck& r, u64 now_ms) {
    auto it = pending_.find(r.op);
    if (it == pending_.end()) return;
    PendingOp& op = it->second;
    if (!r.ok) {
        stats_.lifecycle_conflicts.fetch_add(1, std::memory_order_relaxed);
        if (op.kind == PendingOp::Kind::reg || op.kind == PendingOp::Kind::update || op.kind == PendingOp::Kind::unregister)
            reply_error(op.shard, op.conn, op.req_id, wire::ErrorCode::conflict, "concurrent update, retry", 100);
        // Retry reaping once the competing write has been applied.
        if (op.kind == PendingOp::Kind::tombstone || op.kind == PendingOp::Kind::unregister)
            if (auto f = by_id_.find(op.id); f != by_id_.end())
                wheel_.schedule(&timers_[f->second]->grace, now_ms + 100);
        pending_.erase(it);
        return;
    }
    const Record* rec = find(op.id);
    bool applied;
    if (op.kind == PendingOp::Kind::unregister || op.kind == PendingOp::Kind::tombstone) {
        auto oi = owners_.find(op.id);
        applied = !rec && oi != owners_.end() && oi->second.last_seq >= r.stream_seq;
    } else {
        applied = rec && rec->stream_seq >= r.stream_seq;
    }
    if (applied) {
        finish_op(op, now_ms);
        pending_.erase(it);
    } else {
        op.acked_seq = r.stream_seq;
    }
}

bool Replica::has_pending(const Uuid& id) const {
    for (const auto& [opid, op] : pending_)
        if (op.id == id) return true;
    return false;
}

void Replica::complete_pending_for(const Uuid& id, u64 applied_seq, u64 now_ms) {
    inplace_vector<u64, 16> done;
    for (auto& [opid, op] : pending_)
        if (op.id == id && op.acked_seq != 0 && op.acked_seq <= applied_seq && done.size() < done.capacity())
            done.push_back(opid);
    for (u64 opid : done) {
        auto it = pending_.find(opid);
        finish_op(it->second, now_ms);
        pending_.erase(it);
    }
}

void Replica::finish_op(PendingOp& op, u64) {
    switch (op.kind) {
        case PendingOp::Kind::reg: {
            auto it = by_id_.find(op.id);
            if (it == by_id_.end()) return reply_error(op.shard, op.conn, op.req_id, wire::ErrorCode::internal, "registration lost");
            Record& rec = *records_[it->second];
            if (rec.shard != kNoShard && (rec.shard != op.shard || rec.conn != op.conn))
                reply(rec.shard, rec.conn, HostSupersededReply{});
            rec.shard = op.shard;
            rec.conn = op.conn;
            wire::HostRegistered msg{.req_id = op.req_id,
                                     .handle = rec.handle,
                                     .heartbeat_ms = cfg_.heartbeat_ms,
                                     .ttl_ms = cfg_.ttl_ms,
                                     .observed_address = ip_bytes(rec.addr)};
            if (op.return_token) msg.token = op.token;
            reply(op.shard, op.conn, HostRegisteredReply{.msg = std::move(msg), .handle = rec.handle});
            break;
        }
        case PendingOp::Kind::update:
        case PendingOp::Kind::unregister:
            if (op.req_id) reply(op.shard, op.conn, wire::Ack{.req_id = op.req_id});
            break;
        case PendingOp::Kind::tombstone:
        case PendingOp::Kind::persist: break;
    }
}

void Replica::on(u16, u64, BackboneEdgeDown& r, u64 now_ms) {
    std::vector<u32> orphans;
    for (auto& [id, h] : by_id_)
        if (records_[h]->owner_edge == r.edge_id) orphans.push_back(h);
    for (u32 h : orphans) {
        Record& rec = *records_[h];
        // Every edge hides the orphans locally right away; only the elected reaper writes tombstones.
        if (rec.online) mutate(rec, now_ms, [](Record& x) { x.online = false; });
        if (r.reap) wheel_.schedule(&timers_[h]->grace, now_ms + cfg_.grace_ms);
    }
}

void Replica::fire_timer(TimerNode* n, u64 now_ms) {
    const u32 h = static_cast<u32>(n->cookie);
    Record* rec = by_handle(h);
    if (!rec) return;
    RecordTimers& t = *timers_[h];
    if (n == &t.grace) {
        // Tombstone only if nobody re-registered in the meantime; wait out in-flight writes so
        // the compare-and-set sees the latest sequence.
        if (rec->online || rec->shard != kNoShard) return;
        if (has_pending(rec->id)) return wheel_.schedule(&t.grace, now_ms + 100);
        write_tombstone(*rec, PendingOp{.kind = PendingOp::Kind::tombstone});
    } else if (n == &t.persist) {
        if (rec->owner_edge != cfg_.edge_id) return;
        if (has_pending(rec->id)) return wheel_.schedule(&t.persist, now_ms + cfg_.persist_interval_ms);
        write_record(*rec, to_replicated(*rec), PendingOp{.kind = PendingOp::Kind::persist, .id = rec->id});
    } else if (n == &t.probe) {
        if (rec->online) request_probe(*rec, now_ms);
    }
}

}  // namespace sb::registry
