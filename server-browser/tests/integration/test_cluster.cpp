// Multi-edge tests over a real NATS JetStream server (SB_NATS_URL, e.g. nats://127.0.0.1:4222).

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
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

std::optional<std::string> nats_url() {
    const char* u = std::getenv("SB_NATS_URL");
    if (!u || !*u) return std::nullopt;
    return std::string(u);
}

EdgeOptions clustered(u64 id, const std::string& url, const std::string& stream) {
    return EdgeOptions{.edge_id = id,
                       .backbone = "nats",
                       .nats = {url},
                       .extra_toml = "stream = \"" + stream + "\"\nlease_bucket = \"" + stream + "_EDGES\"\nsubject_prefix = \"" +
                                     stream + "\"\n"};
}

}  // namespace

TEST_CASE("updates on one edge reach browsers on another", "[cluster]") {
    auto url = nats_url();
    if (!url) SKIP("SB_NATS_URL not set");
    const std::string stream = "T" + std::to_string(::getpid()) + "A";
    EdgeProcess a(clustered(101, *url, stream));
    EdgeProcess b(clustered(102, *url, stream));

    TestClient host(runtime(), a.port(), wire::Role::host);
    REQUIRE(host.wait<wire::Welcome>());
    host.c().host_register(host_msg(1));
    REQUIRE(host.wait<wire::HostRegistered>());

    TestClient browser(runtime(), b.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().subscribe({}, 50);
    auto open = browser.wait<wire::SubOpen>();
    REQUIRE(open);
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) { return m.size() == 1; }, 10s));

    std::vector<u64> sent(51);
    for (u32 p = 1; p <= 50; ++p) {
        sent[p] = mono_ns();
        host.c().host_update(wire::HostUpdate{.players = p}, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) {
        for (const auto& e : m.sorted(wire::Sort::players))
            if (e.players == 50) return true;
        return false;
    }));
    std::vector<u64> lat;
    for (const auto& [at, p] : browser.patches())
        if (p.players && *p.players >= 1 && *p.players <= 50) lat.push_back((at - sent[*p.players]) / 1000);
    std::sort(lat.begin(), lat.end());
    REQUIRE_FALSE(lat.empty());
    INFO("cross-edge p50 " << lat[lat.size() / 2] << "us p99 " << lat[lat.size() * 99 / 100] << "us");
    CHECK(lat[lat.size() * 99 / 100] < 100'000);
}

TEST_CASE("entries of a crashed edge are reaped by the survivors", "[cluster]") {
    auto url = nats_url();
    if (!url) SKIP("SB_NATS_URL not set");
    const std::string stream = "T" + std::to_string(::getpid()) + "B";
    auto a = std::make_unique<EdgeProcess>(clustered(201, *url, stream));
    EdgeProcess b(clustered(202, *url, stream));

    auto host = std::make_unique<TestClient>(runtime(), a->port(), wire::Role::host);
    REQUIRE(host->wait<wire::Welcome>());
    host->c().host_register(host_msg(2));
    REQUIRE(host->wait<wire::HostRegistered>());

    TestClient browser(runtime(), b.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().subscribe({}, 50);
    auto open = browser.wait<wire::SubOpen>();
    REQUIRE(open);
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) { return m.size() == 1; }, 10s));

    a->kill();
    host.reset();
    // Hidden as soon as the lease lapses (2 s), tombstoned after the grace period (2 s).
    REQUIRE(browser.wait_mirror(open->view_id, [](const client::ViewMirror& m) { return m.size() == 0; }, 15s));
    std::this_thread::sleep_for(4s);
    browser.c().resolve(host_msg(2).id);
    auto res = browser.wait<wire::ResolveResult>();
    REQUIRE(res);
    CHECK_FALSE(res->details);
}

TEST_CASE("a host migrates between edges with its token", "[cluster]") {
    auto url = nats_url();
    if (!url) SKIP("SB_NATS_URL not set");
    const std::string stream = "T" + std::to_string(::getpid()) + "C";
    EdgeProcess a(clustered(301, *url, stream));
    EdgeProcess b(clustered(302, *url, stream));

    auto reg = host_msg(3);
    wire::Token token{};
    {
        TestClient host(runtime(), a.port(), wire::Role::host);
        REQUIRE(host.wait<wire::Welcome>());
        host.c().host_register(reg);
        auto r = host.wait<wire::HostRegistered>();
        REQUIRE(r);
        token = *r->token;
    }
    TestClient moved(runtime(), b.port(), wire::Role::host);
    REQUIRE(moved.wait<wire::Welcome>());
    reg.token = token;
    moved.c().host_register(reg);
    REQUIRE(moved.wait<wire::HostRegistered>({}, 10s));

    TestClient browser(runtime(), a.port(), wire::Role::browser);
    REQUIRE(browser.wait<wire::Welcome>());
    browser.c().resolve(reg.id);
    auto res = browser.wait<wire::ResolveResult>();
    REQUIRE(res);
    REQUIRE(res->details);
    CHECK(res->details->entry.flags & wire::entry_flag::online);
}
