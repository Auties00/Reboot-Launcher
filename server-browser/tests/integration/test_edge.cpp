#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <thread>

#include "core/time.hpp"
#include "harness.hpp"

using namespace sb;
using namespace sb::test;

namespace {

client::ClientRuntime& runtime() {
    static client::ClientRuntime rt({.insecure = true, .idle_timeout_ms = 30'000, .max_ack_delay_ms = 25});
    return rt;
}

u64 percentile(std::vector<u64> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, static_cast<std::size_t>(p / 100.0 * static_cast<double>(v.size())))];
}

}  // namespace

TEST_CASE("player counts reach browsers in real time", "[edge]") {
    EdgeProcess edge({});
    TestClient host(runtime(), edge.port(), wire::Role::host);
    REQUIRE(host.wait<wire::Welcome>());
    auto reg = host_msg(1);
    host.c().host_register(reg);
    auto registered = host.wait<wire::HostRegistered>();
    REQUIRE(registered);
    REQUIRE(registered->token);

    TestClient browser(runtime(), edge.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().subscribe({}, 50);
    auto open = browser.wait<wire::SubOpen>();
    REQUIRE(open);
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) { return m.size() == 1; }));

    // 100 updates, 10 ms apart: each one must arrive, and quickly.
    std::vector<u64> sent(101);
    for (u32 p = 1; p <= 100; ++p) {
        sent[p] = mono_ns();
        host.c().host_update(wire::HostUpdate{.players = p}, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(browser.wait_mirror(open->view_id, [&](const client::ViewMirror& m) {
        const auto* it = m.find(registered->handle);
        return it && it->entry.players == 100;
    }));
    std::vector<u64> lat_us;
    for (const auto& [at, patch] : browser.patches())
        if (patch.players && *patch.players >= 1 && *patch.players <= 100) lat_us.push_back((at - sent[*patch.players]) / 1000);
    INFO("received " << lat_us.size() << " player patches; p50 " << percentile(lat_us, 50) << "us p99 " << percentile(lat_us, 99) << "us");
    CHECK(lat_us.size() >= 90);  // no batching: almost every update is its own datagram at this rate
    CHECK(percentile(lat_us, 99) < 50'000);
}

TEST_CASE("joins reveal the address only with the right password", "[edge]") {
    EdgeProcess edge({});
    TestClient host(runtime(), edge.port(), wire::Role::host);
    REQUIRE(host.wait<wire::Welcome>());
    auto reg = host_msg(2);
    reg.password = "hunter2";
    host.c().host_register(reg);
    REQUIRE(host.wait<wire::HostRegistered>());

    TestClient browser(runtime(), edge.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().join(reg.id, std::nullopt);
    auto e1 = browser.wait<wire::Error>();
    REQUIRE(e1);
    CHECK(e1->code == wire::ErrorCode::wrong_password);
    browser.c().join(reg.id, std::string("wrong"));
    auto e2 = browser.wait<wire::Error>();
    REQUIRE(e2);
    CHECK(e2->code == wire::ErrorCode::wrong_password);
    browser.c().join(reg.id, std::string("hunter2"));
    auto grant = browser.wait<wire::JoinGrant>();
    REQUIRE(grant);
    CHECK(grant->address == wire::Bytes{127, 0, 0, 1});
    CHECK(grant->port == reg.game_port);
    CHECK(grant->ticket.size() == 16);

    // Listings never carry the address or the password hash.
    browser.c().query({}, "", 10);
    auto q = browser.wait<wire::QueryResult>();
    REQUIRE(q);
    REQUIRE(q->entries.size() == 1);
    CHECK(q->entries[0].flags & wire::entry_flag::has_password);
}

TEST_CASE("a host that disconnects disappears from views immediately", "[edge]") {
    EdgeProcess edge({});
    auto host = std::make_unique<TestClient>(runtime(), edge.port(), wire::Role::host);
    REQUIRE(host->wait<wire::Welcome>());
    host->c().host_register(host_msg(3));
    auto reg = host->wait<wire::HostRegistered>();
    REQUIRE(reg);

    TestClient browser(runtime(), edge.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().subscribe({}, 50);
    auto open = browser.wait<wire::SubOpen>();
    REQUIRE(open);
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) { return m.size() == 1; }));

    const auto t0 = std::chrono::steady_clock::now();
    host.reset();  // closes the connection
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) { return m.size() == 0; }, 3s));
    CHECK(std::chrono::steady_clock::now() - t0 < 1s);

    // Still resolvable (offline) during the grace period, then gone.
    browser.c().resolve(host_msg(3).id);
    auto res = browser.wait<wire::ResolveResult>();
    REQUIRE(res);
    REQUIRE(res->details);
    CHECK_FALSE(res->details->entry.flags & wire::entry_flag::online);
}

TEST_CASE("the stream fallback converges without datagrams", "[edge]") {
    EdgeProcess edge({});
    TestClient host(runtime(), edge.port(), wire::Role::host);
    REQUIRE(host.wait<wire::Welcome>());
    host.c().host_register(host_msg(4));
    auto reg = host.wait<wire::HostRegistered>();
    REQUIRE(reg);

    TestClient browser(runtime(), edge.port(), wire::Role::browser, wire::feature::zstd);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().subscribe({}, 50);
    auto open = browser.wait<wire::SubOpen>();
    REQUIRE(open);
    for (u32 p = 1; p <= 30; ++p) host.c().host_update(wire::HostUpdate{.players = p}, false);
    REQUIRE(browser.wait_mirror(open->view_id, [&](const client::ViewMirror& m) {
        const auto* it = m.find(reg->handle);
        return it && it->entry.players == 30;
    }));
}

TEST_CASE("text search, resolve and hidden entries", "[edge]") {
    EdgeProcess edge({});
    TestClient host_a(runtime(), edge.port(), wire::Role::host);
    TestClient host_b(runtime(), edge.port(), wire::Role::host);
    REQUIRE(host_a.wait<wire::Welcome>());
    REQUIRE(host_b.wait<wire::Welcome>());
    host_a.c().host_register(host_msg(5, "Zone Wars Practice"));
    auto hidden = host_msg(6, "Secret Lobby");
    hidden.hidden = true;
    host_b.c().host_register(hidden);
    REQUIRE(host_a.wait<wire::HostRegistered>());
    REQUIRE(host_b.wait<wire::HostRegistered>());

    TestClient browser(runtime(), edge.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    std::optional<wire::QueryResult> found;
    for (int i = 0; i < 20 && (!found || found->entries.empty()); ++i) {
        browser.c().query({}, "zone w", 10);
        found = browser.wait<wire::QueryResult>();
        std::this_thread::sleep_for(50ms);
    }
    REQUIRE(found);
    REQUIRE(found->entries.size() == 1);
    CHECK(found->entries[0].name == "Zone Wars Practice");

    browser.c().query({}, "secret", 10);
    auto none = browser.wait<wire::QueryResult>();
    REQUIRE(none);
    CHECK(none->entries.empty());

    browser.c().resolve(hidden.id);
    auto res = browser.wait<wire::ResolveResult>();
    REQUIRE(res);
    REQUIRE(res->details);
    CHECK(res->details->entry.name == "Secret Lobby");
}

TEST_CASE("draining edges tell clients to move", "[edge]") {
    EdgeProcess edge({});
    TestClient browser(runtime(), edge.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    edge.terminate();
    auto ga = browser.wait<wire::GoAway>({}, 10s);
    REQUIRE(ga);
    CHECK(ga->reconnect_after_ms <= 1000);
    CHECK(edge.wait_exit(15s));
}

TEST_CASE("ownership tokens protect entries across reconnects", "[edge]") {
    EdgeProcess edge({});
    auto reg = host_msg(7);
    wire::Token token{};
    u64 handle = 0;
    {
        TestClient host(runtime(), edge.port(), wire::Role::host);
        REQUIRE(host.wait<wire::Welcome>());
        host.c().host_register(reg);
        auto r = host.wait<wire::HostRegistered>();
        REQUIRE(r);
        REQUIRE(r->token);
        token = *r->token;
        handle = r->handle;
    }
    TestClient thief(runtime(), edge.port(), wire::Role::host);
    REQUIRE(thief.wait<wire::Welcome>());
    thief.c().host_register(reg);
    auto err = thief.wait<wire::Error>();
    REQUIRE(err);
    CHECK(err->code == wire::ErrorCode::unauthorized);

    TestClient owner(runtime(), edge.port(), wire::Role::host);
    REQUIRE(owner.wait<wire::Welcome>());
    auto again = reg;
    again.token = token;
    owner.c().host_register(again);
    auto r2 = owner.wait<wire::HostRegistered>();
    REQUIRE(r2);
    CHECK(r2->handle == handle);
    CHECK_FALSE(r2->token);
}
