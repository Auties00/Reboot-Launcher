#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "core/broadcast_ring.hpp"
#include "core/mpsc.hpp"
#include "core/rate_limit.hpp"
#include "core/slot_map.hpp"
#include "core/timer_wheel.hpp"

using namespace sb;

TEST_CASE("timer wheel fires in order and supports reschedule and cancel", "[core]") {
    TimerWheel w(100, 64);
    w.start(1000);
    TimerNode a, b, c;
    a.cookie = 1;
    b.cookie = 2;
    c.cookie = 3;
    w.schedule(&a, 1250);
    w.schedule(&b, 1150);
    w.schedule(&c, 1000 + 100 * 64 * 3);  // several wheel rounds away
    std::vector<u64> fired;
    auto fire = [&](TimerNode* n) { fired.push_back(n->cookie); };
    w.advance(1200, fire);
    CHECK(fired == std::vector<u64>{2});
    w.schedule(&a, 1600);  // reschedule moves it later
    w.advance(1300, fire);
    CHECK(fired.size() == 1);
    w.advance(1700, fire);
    CHECK(fired == std::vector<u64>{2, 1});
    w.cancel(&c);
    w.advance(1000 + 100 * 64 * 4, fire);
    CHECK(fired.size() == 2);
    CHECK(w.size() == 0);
}

TEST_CASE("token bucket refills exactly, even at sub-token rates", "[core]") {
    TokenBucket b(RateSpec::per_minute(5, 5));  // one token every 12 s
    u64 t = 1'000;
    for (int i = 0; i < 5; ++i) CHECK(b.take(t));
    CHECK_FALSE(b.take(t));
    CHECK(b.wait_ms(t) >= 12'000);
    CHECK(b.wait_ms(t) <= 12'001);
    for (int i = 0; i < 1201; ++i) {
        t += 10;  // frequent polling must not lose fractional refill
        (void)b.wait_ms(t);
    }
    CHECK(b.take(t));
    CHECK_FALSE(b.take(t));
}

TEST_CASE("atomic rate table limits per key and refills", "[core]") {
    AtomicRateTable table(1024, RateSpec::per_second(2, 4));
    u64 t = 5'000;
    int ok = 0;
    for (int i = 0; i < 10; ++i) ok += table.take(42, t);
    CHECK(ok == 4);
    for (int i = 0; i < 100; ++i) {
        t += 10;
        ok += table.take(42, t);
    }
    CHECK(ok == 4 + 2);  // one second at 2/s
    CHECK(table.take(43, t));  // other keys are independent
}

TEST_CASE("slot map rejects stale handles", "[core]") {
    SlotMap<int> m;
    auto [h1, v1] = m.emplace(7);
    CHECK(*m.get(h1) == 7);
    m.erase(h1);
    auto [h2, v2] = m.emplace(8);
    CHECK(h2.index == h1.index);
    CHECK(m.get(h1) == nullptr);
    CHECK(*m.get(h2) == 8);
    CHECK(SlotHandle::unpack(h2.pack()) == h2);
}

namespace {
struct Item : MpscNode {
    int producer = 0;
    int seq = 0;
};
}  // namespace

TEST_CASE("mpsc queue keeps per-producer order under contention", "[core]") {
    MpscQueue q;
    constexpr int kProducers = 4, kPer = 50'000;
    std::vector<std::jthread> ps;
    for (int p = 0; p < kProducers; ++p)
        ps.emplace_back([&, p] {
            for (int i = 0; i < kPer; ++i) {
                auto* it = new Item();
                it->producer = p;
                it->seq = i;
                q.push(it);
            }
        });
    std::vector<int> last(kProducers, -1);
    int got = 0;
    while (got < kProducers * kPer) {
        auto* n = static_cast<Item*>(q.pop());
        if (!n) continue;
        REQUIRE(n->seq == last[n->producer] + 1);
        last[n->producer] = n->seq;
        ++got;
        delete n;
    }
}

TEST_CASE("broadcast ring delivers every event to every consumer in order", "[core]") {
    BroadcastRing<u64> ring(256);
    constexpr int kConsumers = 3;
    constexpr u64 kEvents = 200'000;
    std::vector<Waker> wakers(kConsumers);
    std::vector<BroadcastRing<u64>::Consumer*> cs;
    for (auto& w : wakers) cs.push_back(&ring.add_consumer(&w));
    std::atomic<int> failures{0};
    std::vector<std::jthread> threads;
    for (int i = 0; i < kConsumers; ++i)
        threads.emplace_back([&, i] {
            u64 expect = 0;
            while (expect < kEvents) {
                const u64 avail = ring.available();
                u64 seq = cs[i]->cursor.load();
                if (seq == avail) {
                    wakers[i].announce_sleep();
                    if (ring.available() != seq) {
                        wakers[i].cancel_sleep();
                        continue;
                    }
                    wakers[i].block(std::chrono::milliseconds(10));
                    continue;
                }
                for (; seq < avail; ++seq)
                    if (ring.at(seq) != expect++) failures.fetch_add(1);
                BroadcastRing<u64>::advance(*cs[i], avail);
            }
        });
    for (u64 v = 0; v < kEvents; ++v) {
        ring.publish(v);
        if ((v & 63) == 0) ring.wake_all();
    }
    ring.wake_all();
    threads.clear();
    CHECK(failures.load() == 0);
    CHECK(ring.min_cursor() == kEvents);
}
