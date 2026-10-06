#include <catch2/catch_test_macros.hpp>

#include <map>
#include <random>
#include <set>

#include "backbone/inproc.hpp"
#include "client/view_mirror.hpp"
#include "registry/replica.hpp"
#include "security/crypto.hpp"
#include "wire/frame.hpp"

using namespace sb;
using namespace sb::registry;

namespace {

// Drives a replica synchronously and plays the role of shard 0 on the ring.
struct Harness {
    backbone::InprocBackbone bb;
    Replica rep;
    Waker waker;
    BroadcastRing<RingEvent*>::Consumer& consumer;
    u64 now = 1'000'000;
    std::vector<ReplyPayload> replies;  // replies addressed to shard 0, in order
    std::map<u32, client::ViewMirror> mirrors;  // view id -> client state
    std::map<u32, client::ViewMirror> lagging;  // same views, but receiving only 1 frame in 10
    std::map<u32, u64> max_vseq;                // latest frame sequence seen per view
    std::mt19937 drop_rng{99};
    std::set<u32> subscribed;
    u64 frames_seen = 0;

    static ReplicaConfig config(std::vector<u32> windows = {50, 200}) {
        ReplicaConfig c;
        c.max_entries = 1 << 14;
        c.num_shards = 1;
        c.ring_capacity = 1 << 12;
        c.probe_enabled = false;
        c.windows = std::move(windows);
        c.max_hosts_per_ip = 1000;
        return c;
    }

    explicit Harness(ReplicaConfig cfg = config()) : rep(std::move(cfg), bb), consumer(rep.ring().add_consumer(&waker)) {}

    ~Harness() { pump(); }

    void post(u64 conn, ReplicaPayload p) { rep.post(0, conn, std::move(p)); }

    void pump() {
        while (rep.poll_once(now)) drain_ring();
        drain_ring();
        rep.poll_once(now);  // reclaim
    }

    void advance(u64 ms) {
        now += ms;
        pump();
    }

    void drain_ring() {
        auto& ring = rep.ring();
        const u64 avail = ring.available();
        for (u64 s = consumer.cursor.load(); s < avail; ++s) {
            RingEvent* ev = ring.at(s);
            for (Frame* f : ev->frames) {
                ++frames_seen;
                if (subscribed.count(f->view_id)) {
                    bool ok = wire::for_each_frame(std::span<const u8>(f->buf.data, f->buf.length), 1500,
                                                   [&](const wire::FrameView& fv) {
                                                       wire::Delta d;
                                                       REQUIRE(wire::decode_frame(fv, d));
                                                       REQUIRE(d.view_id == f->view_id);
                                                       mirrors[d.view_id].on_delta(d);
                                                       max_vseq[d.view_id] = std::max(max_vseq[d.view_id], f->vseq);
                                                       if (drop_rng() % 10 == 0) lagging[d.view_id].on_delta(d);
                                                       return true;
                                                   });
                    REQUIRE(ok);
                }
                Frame::release(f);
            }
            if (ev->kind == RingEvent::Kind::reply && ev->target == 0) {
                if (auto* sr = std::get_if<SubscribeReply>(&*ev->reply)) {
                    subscribed.insert(sr->view_id);
                    wire::Snapshot snap;
                    bool ok = wire::for_each_frame(sr->snapshot->frame, 1 << 24, [&](const wire::FrameView& fv) {
                        REQUIRE(wire::decode_frame(fv, snap));
                        return true;
                    });
                    REQUIRE(ok);
                    mirrors[sr->view_id].on_snapshot(snap);
                    lagging[sr->view_id].on_snapshot(snap);
                    max_vseq[sr->view_id] = std::max(max_vseq[sr->view_id], snap.vseq);
                }
                replies.push_back(*ev->reply);
            }
        }
        BroadcastRing<RingEvent*>::advance(consumer, avail);
    }

    template <class T>
    T take_reply() {
        REQUIRE_FALSE(replies.empty());
        auto r = std::move(replies.front());
        replies.erase(replies.begin());
        if (auto* e = std::get_if<wire::Error>(&r)) FAIL("unexpected error: " << e->message);
        REQUIRE(std::holds_alternative<T>(r));
        return std::get<T>(std::move(r));
    }

    wire::Error take_error() {
        REQUIRE_FALSE(replies.empty());
        auto r = std::move(replies.front());
        replies.erase(replies.begin());
        REQUIRE(std::holds_alternative<wire::Error>(r));
        return std::get<wire::Error>(r);
    }
};

Uuid uuid_n(u32 n) {
    Uuid u;
    for (int i = 0; i < 4; ++i) u.bytes[12 + i] = static_cast<u8>(n >> (24 - 8 * i));
    u.bytes[0] = 0xAB;
    return u;
}

HostRegisterReq reg_req(u32 n, u32 players = 0, std::string name = {}) {
    HostRegisterReq r;
    r.msg.req_id = n;
    r.msg.id = uuid_n(n);
    r.msg.name = name.empty() ? "server " + std::to_string(n) : std::move(name);
    r.msg.version = n % 2 ? "4.5" : "1.7.2";
    r.msg.author = "author" + std::to_string(n % 7);
    r.msg.game_port = 7777;
    r.msg.max_players = 100;
    r.msg.players = players;
    r.addr = IpAddr::v4(0x0A000000u + n);
    security::random_bytes(r.new_token);
    r.new_token_hash = security::sha256(r.new_token);
    return r;
}

wire::ViewSpec all_players() { return {}; }

}  // namespace

TEST_CASE("register, subscribe and receive live player updates", "[replica]") {
    Harness h;
    h.post(1, reg_req(1, 5));
    h.pump();
    auto reg = h.take_reply<HostRegisteredReply>();
    REQUIRE(reg.msg.token.has_value());
    CHECK(reg.handle != 0);
    CHECK(h.rep.size() == 1);

    h.post(2, SubscribeReq{.req_id = 9, .sub_id = 1, .spec = all_players(), .window = 50});
    h.pump();
    auto sub = h.take_reply<SubscribeReply>();
    CHECK(sub.window == 50);
    REQUIRE(h.mirrors[sub.view_id].size() == 1);

    h.post(1, HostUpdateReq{.req_id = 0, .handle = reg.handle, .msg = {.players = 42u}});
    h.pump();
    const auto* item = h.mirrors[sub.view_id].find(reg.handle);
    REQUIRE(item);
    CHECK(item->entry.players == 42);

    // Only the owning connection may update.
    h.post(3, HostUpdateReq{.req_id = 5, .handle = reg.handle, .msg = {.players = 1u}});
    h.pump();
    CHECK(h.take_error().code == wire::ErrorCode::unauthorized);
}

TEST_CASE("windowed views converge under random churn", "[replica]") {
    for (u32 window : {3u, 50u}) {
        Harness h(Harness::config({window}));
        std::mt19937 rng(window);
        std::map<u32, u32> handles;  // n -> handle
        std::map<u32, u64> conns;
        u32 next = 1;
        auto add = [&] {
            const u32 n = next++;
            h.post(1000 + n, reg_req(n, rng() % 64));
            h.pump();
            handles[n] = h.take_reply<HostRegisteredReply>().handle;
            conns[n] = 1000 + n;
        };
        for (int i = 0; i < 40; ++i) add();

        // Subscribe to several views before and during churn.
        std::vector<std::pair<u32, wire::ViewSpec>> views;
        auto subscribe = [&](wire::ViewSpec spec) {
            h.post(1, SubscribeReq{.req_id = 1, .sub_id = 1, .spec = spec, .window = window});
            h.pump();
            views.emplace_back(h.take_reply<SubscribeReply>().view_id, spec);
        };
        subscribe({});
        subscribe({.sort = wire::Sort::name});
        subscribe({.bucket = version_bucket("4.5"), .sort = wire::Sort::newest});
        subscribe({.password = wire::PasswordFilter::none, .sort = wire::Sort::players});

        for (int step = 0; step < 2000; ++step) {
            const int op = static_cast<int>(rng() % 100);
            if (op < 60 && !handles.empty()) {
                auto it = std::next(handles.begin(), static_cast<long>(rng() % handles.size()));
                h.post(conns[it->first], HostUpdateReq{.handle = it->second, .msg = {.players = static_cast<u32>(rng() % 64)}});
            } else if (op < 70 && !handles.empty()) {
                auto it = std::next(handles.begin(), static_cast<long>(rng() % handles.size()));
                h.post(conns[it->first], HostUpdateReq{.handle = it->second, .msg = {.name = "n" + std::to_string(rng() % 1000)}});
            } else if (op < 78) {
                add();
            } else if (op < 86 && !handles.empty()) {
                auto it = std::next(handles.begin(), static_cast<long>(rng() % handles.size()));
                h.post(conns[it->first], HostUnregisterReq{.req_id = 77, .handle = it->second});
                h.pump();
                (void)h.take_reply<wire::Ack>();
                conns.erase(it->first);
                handles.erase(it);
            } else if (op < 93 && !handles.empty()) {
                auto it = std::next(handles.begin(), static_cast<long>(rng() % handles.size()));
                h.post(conns[it->first], HostUpdateReq{.handle = it->second, .msg = {.hidden = (rng() % 2) == 0}});
            } else if (op < 95) {
                subscribe({.region = wire::Region::all, .sort = wire::Sort::players});
            }
            h.pump();
            REQUIRE(h.replies.empty());
        }

        // Every mirror must equal the authoritative first page of the same view.
        for (const auto& [view_id, spec] : views) {
            const auto got = h.mirrors[view_id].sorted(spec.sort);
            h.post(2, QueryReq{.req_id = 2, .spec = spec, .limit = window});
            h.pump();
            const auto want = h.take_reply<wire::QueryResult>();
            INFO("view " << view_id << " window " << window);
            REQUIRE(got.size() == want.entries.size());
            for (std::size_t i = 0; i < got.size(); ++i) {
                CHECK(got[i].handle == want.entries[i].handle);
                CHECK(got[i].players == want.entries[i].players);
                CHECK(got[i].name == want.entries[i].name);
                CHECK(got[i].flags == want.entries[i].flags);
            }

            // The published window list is exactly the first page.
            const WindowList* wl = h.rep.window(view_id);
            REQUIRE(wl);
            std::vector<u32> members(wl->handles.begin(), wl->handles.end()), expect;
            for (const auto& e : want.entries) expect.push_back(static_cast<u32>(e.handle));
            std::sort(members.begin(), members.end());
            std::sort(expect.begin(), expect.end());
            CHECK(members == expect);

            // A client that lost 90% of frames catches up from one WindowSync plus the current
            // values of the members, which is what a shard sends a lagging connection.
            auto& lag = h.lagging[view_id];
            wire::Delta catchup{.view_id = view_id};
            const u64 vseq = h.max_vseq[view_id];
            catchup.sync = wire::WindowSync{.vseq = vseq, .handles = {wl->handles.begin(), wl->handles.end()}};
            for (u32 handle : wl->handles) {
                wire::Patch& p = catchup.patches.emplace_back();
                p.handle = handle;
                p.vseq = vseq;
                h.rep.pub(handle)->to_list_entry(p.entry.emplace());
            }
            lag.on_delta(catchup);
            const auto caught = lag.sorted(spec.sort);
            REQUIRE(caught.size() == want.entries.size());
            for (std::size_t i = 0; i < caught.size(); ++i) {
                CHECK(caught[i].handle == want.entries[i].handle);
                CHECK(caught[i].players == want.entries[i].players);
            }
        }
    }
}

TEST_CASE("mirrors converge with lost and reordered patches plus repair", "[replica]") {
    client::ViewMirror m;
    wire::Snapshot s{.view_id = 1, .vseq = 10, .total = 1};
    s.entries.push_back({.handle = 7, .name = "a", .players = 1});
    m.on_snapshot(s);
    // name change at 12 arrives before the players change at 11
    m.apply({.handle = 7, .vseq = 12, .name = std::string("b")});
    m.apply({.handle = 7, .vseq = 11, .players = 5u});
    REQUIRE(m.find(7));
    CHECK(m.find(7)->entry.players == 5);
    CHECK(m.find(7)->entry.name == "b");
    // stale patch below the snapshot floor is ignored
    m.apply({.handle = 7, .vseq = 9, .players = 99u});
    CHECK(m.find(7)->entry.players == 5);
    // removal then a delayed older insert must not resurrect
    m.apply({.handle = 7, .vseq = 20, .removed = true});
    m.apply({.handle = 7, .vseq = 15, .entry = wire::ListEntry{.handle = 7, .name = "old"}});
    CHECK_FALSE(m.find(7));
    // repair with the same sequence as the latest change is accepted
    m.apply({.handle = 8, .vseq = 21, .entry = wire::ListEntry{.handle = 8, .name = "c", .players = 3}});
    m.apply({.handle = 8, .vseq = 21, .entry = wire::ListEntry{.handle = 8, .name = "c", .players = 3}});
    CHECK(m.size() == 1);
    // A window sync removes members it does not list, but never something newer than itself.
    m.apply({.handle = 9, .vseq = 30, .entry = wire::ListEntry{.handle = 9, .name = "d"}});
    m.apply_sync({.vseq = 25, .handles = {7}});
    CHECK_FALSE(m.find(8));
    CHECK(m.find(9));  // inserted at 30, after the sync's 25
    m.apply({.handle = 8, .vseq = 24, .entry = wire::ListEntry{.handle = 8, .name = "late"}});
    CHECK_FALSE(m.find(8));  // older than the sync that removed it
}

TEST_CASE("keyset paging walks every entry exactly once in order", "[replica]") {
    Harness h;
    for (u32 n = 1; n <= 230; ++n) {
        // Many identical name prefixes exercise the name tie-breaking through the cursor.
        h.post(n, reg_req(n, n % 17, "Same Prefix Server " + std::to_string(n % 13)));
        h.pump();
        (void)h.take_reply<HostRegisteredReply>();
    }
    for (auto sort : {wire::Sort::players, wire::Sort::newest, wire::Sort::name}) {
        std::vector<wire::ListEntry> all;
        wire::Bytes cursor;
        u32 total = 0;
        do {
            h.post(1, QueryReq{.req_id = 1, .spec = {.sort = sort}, .limit = 37, .cursor = cursor});
            h.pump();
            auto res = h.take_reply<wire::QueryResult>();
            total = res.total;
            all.insert(all.end(), res.entries.begin(), res.entries.end());
            cursor = res.next_cursor;
        } while (!cursor.empty());
        REQUIRE(total == 230);
        REQUIRE(all.size() == 230);
        for (std::size_t i = 1; i < all.size(); ++i) CHECK(client::ViewMirror::less(sort, all[i - 1], all[i]));
    }
    h.post(1, QueryReq{.req_id = 1, .spec = {}, .cursor = {1, 2, 3}});
    h.pump();
    CHECK(h.take_error().code == wire::ErrorCode::bad_request);
}

TEST_CASE("ownership tokens, passwords and joins", "[replica]") {
    Harness h;
    const std::string pepper = "pepper";
    auto mac = [&](const Uuid& id, std::string_view pw) {
        return security::hmac_sha256(security::as_bytes(pepper), {std::span<const u8>(id.bytes), security::as_bytes(pw)});
    };

    auto r = reg_req(1);
    r.password_mac = mac(r.msg.id, "secret");
    h.post(10, r);
    h.pump();
    auto reg = h.take_reply<HostRegisteredReply>();
    const wire::Token token = *reg.msg.token;

    // Another host cannot claim the id without the token.
    auto thief = reg_req(1);
    h.post(11, thief);
    h.pump();
    CHECK(h.take_error().code == wire::ErrorCode::unauthorized);

    // Joins need the right password and only then reveal the address.
    h.post(20, JoinReq{.req_id = 1, .id = r.msg.id});
    h.pump();
    CHECK(h.take_error().code == wire::ErrorCode::wrong_password);
    h.post(20, JoinReq{.req_id = 2, .id = r.msg.id, .password_mac = mac(r.msg.id, "nope")});
    h.pump();
    CHECK(h.take_error().code == wire::ErrorCode::wrong_password);
    h.post(20, JoinReq{.req_id = 3, .id = r.msg.id, .password_mac = mac(r.msg.id, "secret")});
    h.pump();
    auto grant = h.take_reply<JoinReply>();
    CHECK(grant.addr == r.addr);
    CHECK(grant.port == 7777);

    // The rightful owner reconnects with the token: the old connection is superseded.
    auto again = reg_req(1);
    again.presented_token_hash = security::sha256(token);
    h.post(12, again);
    h.pump();
    CHECK(std::holds_alternative<HostSupersededReply>(h.replies.front()));
    h.replies.erase(h.replies.begin());
    auto reg2 = h.take_reply<HostRegisteredReply>();
    CHECK_FALSE(reg2.msg.token.has_value());
    CHECK(reg2.handle == reg.handle);
}

TEST_CASE("disconnected hosts disappear immediately and are tombstoned after the grace period", "[replica]") {
    Harness h;
    h.post(1, reg_req(1, 3));
    h.pump();
    auto reg = h.take_reply<HostRegisteredReply>();
    h.post(2, SubscribeReq{.req_id = 1, .sub_id = 1, .spec = {}, .window = 50});
    h.pump();
    auto sub = h.take_reply<SubscribeReply>();
    REQUIRE(h.mirrors[sub.view_id].size() == 1);

    h.post(1, HostGoneReq{.handle = reg.handle});
    h.pump();
    CHECK(h.mirrors[sub.view_id].size() == 0);
    CHECK(h.rep.size() == 1);  // still known during the grace period

    h.post(4, ResolveReq{.req_id = 1, .id = uuid_n(1)});
    h.pump();
    auto res = h.take_reply<wire::ResolveResult>();
    REQUIRE(res.details);
    CHECK_FALSE(res.details->entry.flags & wire::entry_flag::online);

    h.advance(h.rep.config().grace_ms + 200);
    h.advance(500);  // a debounced persist may delay the tombstone by one retry
    CHECK(h.rep.size() == 0);

    // The id stays reserved for its token holder after the tombstone.
    h.post(5, reg_req(1));
    h.pump();
    CHECK(h.take_error().code == wire::ErrorCode::unauthorized);
}
