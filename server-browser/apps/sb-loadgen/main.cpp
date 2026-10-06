// sb-loadgen: drives hosts and browsers against one or more edges and measures end-to-end
// delivery latency (host update -> browser datagram) with a shared in-process clock.

#include <CLI/CLI.hpp>

#include <sys/resource.h>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "client/client.hpp"
#include "client/view_mirror.hpp"
#include "core/time.hpp"
#include "ops/admin_http.hpp"

using namespace sb;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> g_stop{false};

// Concurrent log-linear histogram in microseconds: 64 sub-buckets per power of two (~1.5% error).
class LatencyHistogram {
public:
    void record(u64 us) noexcept {
        counts_[index(us)].fetch_add(1, std::memory_order_relaxed);
        total_.fetch_add(1, std::memory_order_relaxed);
    }
    [[nodiscard]] u64 count() const noexcept { return total_.load(std::memory_order_relaxed); }
    [[nodiscard]] u64 percentile(double p) const noexcept {
        const u64 n = count();
        if (n == 0) return 0;
        const u64 target = static_cast<u64>(p / 100.0 * static_cast<double>(n - 1)) + 1;
        u64 seen = 0;
        for (std::size_t i = 0; i < kBuckets; ++i) {
            seen += counts_[i].load(std::memory_order_relaxed);
            if (seen >= target) return value(i);
        }
        return value(kBuckets - 1);
    }
    void reset() noexcept {
        for (auto& c : counts_) c.store(0, std::memory_order_relaxed);
        total_.store(0, std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kSub = 64;
    static constexpr std::size_t kBuckets = 40 * kSub;
    static std::size_t index(u64 v) noexcept {
        if (v < kSub) return v;
        const unsigned exp = static_cast<unsigned>(std::bit_width(v)) - 7;  // keep the top 6 bits
        const std::size_t i = (exp + 1) * kSub + ((v >> exp) & (kSub - 1));
        return std::min(i, kBuckets - 1);
    }
    static u64 value(std::size_t i) noexcept {
        if (i < kSub) return i;
        const std::size_t exp = i / kSub - 1;
        return ((kSub | (i % kSub)) << exp);
    }
    std::array<std::atomic<u64>, kBuckets> counts_{};
    std::atomic<u64> total_{0};
};

struct Stats {
    std::atomic<u64> connected{0};
    std::atomic<u64> closed{0};
    std::atomic<u64> deltas{0};
    std::atomic<u64> patches{0};
    std::atomic<u64> snapshots{0};
    std::atomic<u64> errors{0};
    std::atomic<u64> updates_sent{0};
    std::atomic<u64> registered{0};
    std::atomic<u64> joins_ok{0};
    std::atomic<u64> queries_ok{0};
    std::atomic<u64> goaways{0};
    LatencyHistogram latency;  // reset every report
    LatencyHistogram total;    // whole steady state
    std::atomic<bool> steady{false};
};

double cpu_seconds() {
    rusage ru{};
    ::getrusage(RUSAGE_SELF, &ru);
    return static_cast<double>(ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) +
           static_cast<double>(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
}

Uuid host_uuid(u32 i, u32 run) {
    Uuid u;
    u.bytes[0] = 0x5B;
    for (int k = 0; k < 4; ++k) u.bytes[4 + k] = static_cast<u8>(run >> (24 - 8 * k));
    for (int k = 0; k < 4; ++k) u.bytes[12 + k] = static_cast<u8>(i >> (24 - 8 * k));
    return u;
}

u32 host_index(const Uuid& u) {
    u32 i = 0;
    for (int k = 0; k < 4; ++k) i = (i << 8) | u.bytes[12 + k];
    return i;
}

// sent_ns[host * 1024 + players % 1024] = when that player count was published.
struct SendLog {
    std::vector<std::atomic<u64>> at;
    explicit SendLog(std::size_t hosts) : at(hosts * 1024) {}
    void mark(u32 host, u32 players, u64 ns) { at[host * 1024 + (players & 1023)].store(ns, std::memory_order_relaxed); }
    [[nodiscard]] u64 when(u32 host, u32 players) const { return at[host * 1024 + (players & 1023)].load(std::memory_order_relaxed); }
};

struct Endpoint {
    std::string host;
    u16 port;
};

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"sb-loadgen: load and latency generator for sb-edge"};
    std::vector<std::string> servers{"127.0.0.1:4433"}, binds;
    u32 hosts = 100, browsers = 1000, subs = 1, window = 50, duration = 30, ramp = 2000, report = 5;
    double update_rate = 1.0, sample = 0.05, join_rate = 0, query_rate = 0;
    u32 run_id = static_cast<u32>(std::random_device{}());
    bool insecure = true, no_datagrams = false;
    std::string summary_path, label = "loadgen";
    app.add_option("-s,--server", servers, "edge addresses (browsers and hosts are spread across them)");
    app.add_option("--hosts", hosts, "host connections, one entry each");
    app.add_option("--update-rate", update_rate, "player-count updates per second per host");
    app.add_option("--browsers", browsers, "browser connections");
    app.add_option("--subs", subs, "subscriptions per browser");
    app.add_option("--window", window, "subscription window");
    app.add_option("--duration", duration, "seconds of steady state");
    app.add_option("--ramp", ramp, "new connections per second while ramping up");
    app.add_option("--sample", sample, "fraction of browsers that keep a mirror and measure latency");
    app.add_option("--join-rate", join_rate, "joins per second (all browsers together)");
    app.add_option("--query-rate", query_rate, "queries per second (all browsers together)");
    app.add_option("--bind", binds, "local source addresses to spread connections over");
    app.add_option("--report", report, "seconds between reports");
    app.add_option("--run-id", run_id, "namespace for host ids (reuse to re-register the same entries)");
    app.add_flag("--no-datagrams", no_datagrams, "force the stream fallback");
    app.add_option("--summary", summary_path, "write a JSON summary of the steady state to this file");
    app.add_option("--label", label, "scenario name for the summary");
    app.add_flag("!--secure", insecure, "validate certificates");
    CLI11_PARSE(app, argc, argv);
    std::signal(SIGINT, [](int) { g_stop = true; });

    std::vector<Endpoint> eps;
    for (const auto& s : servers) {
        auto [h, p] = ops::split_host_port(s);
        eps.push_back({h, static_cast<u16>(p)});
    }
    std::vector<IpAddr> sources;
    for (const auto& b : binds) {
        unsigned a, bb, c, d;
        if (std::sscanf(b.c_str(), "%u.%u.%u.%u", &a, &bb, &c, &d) == 4) sources.push_back(IpAddr::v4((a << 24) | (bb << 16) | (c << 8) | d));
    }

    client::ClientRuntime rt({.insecure = insecure, .idle_timeout_ms = 60'000, .keepalive_ms = 15'000});
    Stats st;
    SendLog log(hosts);
    const u64 features = no_datagrams ? wire::feature::zstd : wire::feature::datagrams | wire::feature::zstd;

    // ---- hosts -------------------------------------------------------------------------------
    // The client is declared last so it is destroyed first: its callbacks use the other members.
    struct HostState {
        std::atomic<bool> ready{false};
        u32 players = 0;
        std::unique_ptr<client::Client> c;
    };
    std::vector<std::unique_ptr<HostState>> host_states;
    host_states.reserve(hosts);
    u32 next_source = 0;
    auto pick_source = [&]() -> std::optional<IpAddr> {
        if (sources.empty()) return std::nullopt;
        return sources[next_source++ % sources.size()];
    };

    std::printf("ramping %u hosts and %u browsers at %u conn/s\n", hosts, browsers, ramp);
    const auto ramp_gap = std::chrono::microseconds(1'000'000 / std::max<u32>(1, ramp));
    for (u32 i = 0; i < hosts && !g_stop; ++i) {
        auto hs = std::make_unique<HostState>();
        HostState* raw = hs.get();
        const Endpoint& ep = eps[i % eps.size()];
        client::Client::Options o{.host = ep.host, .port = ep.port, .role = wire::Role::host, .features = features,
                                  .client_version = "sb-loadgen", .local_address = pick_source()};
        o.on_event = [&, raw, i](client::Event& e) {
            if (std::holds_alternative<client::Connected>(e)) {
                st.connected.fetch_add(1);
                raw->c->host_register(wire::HostRegister{.id = host_uuid(i, run_id),
                                                         .name = "loadgen " + std::to_string(i),
                                                         .version = (i % 3 == 0) ? "4.5" : (i % 3 == 1 ? "10.40" : "1.7.2"),
                                                         .author = "loadgen",
                                                         .game_port = static_cast<u32>(10000 + i % 50000),
                                                         .max_players = 100});
            } else if (std::holds_alternative<wire::HostRegistered>(e)) {
                st.registered.fetch_add(1);
                raw->ready = true;
            } else if (auto* err = std::get_if<wire::Error>(&e)) {
                st.errors.fetch_add(1);
                if (st.errors.load() < 10) std::fprintf(stderr, "host %u error: %s\n", i, err->message.c_str());
            } else if (std::holds_alternative<wire::GoAway>(e)) {
                st.goaways.fetch_add(1);
            } else if (std::holds_alternative<client::Closed>(e)) {
                raw->ready = false;
                st.closed.fetch_add(1);
            }
        };
        hs->c = std::make_unique<client::Client>(rt, std::move(o));
        hs->c->connect();
        host_states.push_back(std::move(hs));
        std::this_thread::sleep_for(ramp_gap);
    }

    // Hosts heartbeat from their own thread so a long browser ramp cannot expire them.
    std::jthread heartbeats([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            for (auto& h : host_states)
                if (h->ready) h->c->heartbeat();
            for (int i = 0; i < 20 && !stop.stop_requested(); ++i) std::this_thread::sleep_for(100ms);
        }
    });

    // ---- browsers ----------------------------------------------------------------------------
    struct BrowserState {
        bool sampled = false;
        std::mutex mu;
        std::vector<std::pair<u32, client::ViewMirror>> mirrors;  // view id -> mirror (sampled only)
        std::unique_ptr<client::Client> c;
    };
    std::vector<std::unique_ptr<BrowserState>> browser_states;
    browser_states.reserve(browsers);
    std::mt19937 rng(run_id);
    const u32 buckets[] = {wire::kBucketAll, 2 + 4 * 1024 + 5, 2 + 10 * 1024 + 40, 2 + 1 * 1024 + 7};
    for (u32 i = 0; i < browsers && !g_stop; ++i) {
        auto bs = std::make_unique<BrowserState>();
        BrowserState* raw = bs.get();
        raw->sampled = std::uniform_real_distribution<double>(0, 1)(rng) < sample;
        const Endpoint& ep = eps[(i + 1) % eps.size()];  // cross-edge when several servers are given
        client::Client::Options o{.host = ep.host, .port = ep.port, .role = wire::Role::browser, .features = features,
                                  .client_version = "sb-loadgen", .local_address = pick_source(),
                                  .decode_deltas = raw->sampled};
        o.on_event = [&, raw, i](client::Event& e) {
            if (std::holds_alternative<client::Connected>(e)) {
                st.connected.fetch_add(1);
                for (u32 s = 0; s < subs; ++s)
                    raw->c->subscribe(wire::ViewSpec{.bucket = buckets[(i + s) % 4], .sort = wire::Sort::players}, window);
            } else if (auto* d = std::get_if<client::DeltaEvent>(&e)) {
                st.deltas.fetch_add(1, std::memory_order_relaxed);
                st.patches.fetch_add(d->patch_count, std::memory_order_relaxed);
                if (!raw->sampled) return;
                const u64 now = mono_ns();
                std::lock_guard lk(raw->mu);
                for (auto& [vid, m] : raw->mirrors) {
                    if (vid != d->delta.view_id) continue;
                    m.on_delta(d->delta);
                    for (const auto& p : d->delta.patches) {
                        if (!p.players) continue;
                        const auto* it = m.find(p.handle);
                        if (!it || it->entry.id.bytes[0] != 0x5B) continue;
                        const u32 h = host_index(it->entry.id);
                        if (h >= hosts) continue;
                        const u64 sent = log.when(h, *p.players);
                        if (sent && now > sent && now - sent < 10'000'000'000ull) {
                            st.latency.record((now - sent) / 1000);
                            if (st.steady.load(std::memory_order_relaxed)) st.total.record((now - sent) / 1000);
                        }
                    }
                }
            } else if (auto* s = std::get_if<client::SnapshotEvent>(&e)) {
                st.snapshots.fetch_add(1, std::memory_order_relaxed);
                if (!raw->sampled) return;
                std::lock_guard lk(raw->mu);
                for (auto& [vid, m] : raw->mirrors)
                    if (vid == s->snapshot.view_id) {
                        m.on_snapshot(s->snapshot);
                        return;
                    }
                raw->mirrors.emplace_back(s->snapshot.view_id, client::ViewMirror{});
                raw->mirrors.back().second.on_snapshot(s->snapshot);
            } else if (std::holds_alternative<wire::JoinGrant>(e)) {
                st.joins_ok.fetch_add(1);
            } else if (std::holds_alternative<wire::QueryResult>(e)) {
                st.queries_ok.fetch_add(1);
            } else if (std::holds_alternative<wire::Error>(e)) {
                st.errors.fetch_add(1);
            } else if (std::holds_alternative<wire::GoAway>(e)) {
                st.goaways.fetch_add(1);
            } else if (std::holds_alternative<client::Closed>(e)) {
                st.closed.fetch_add(1);
            }
        };
        bs->c = std::make_unique<client::Client>(rt, std::move(o));
        bs->c->connect();
        browser_states.push_back(std::move(bs));
        std::this_thread::sleep_for(ramp_gap);
    }
    std::printf("ramp complete: %llu connected, %llu hosts registered\n", static_cast<unsigned long long>(st.connected.load()),
                static_cast<unsigned long long>(st.registered.load()));

    // ---- steady state ------------------------------------------------------------------------
    std::this_thread::sleep_for(2s);  // let the last handshakes and snapshots settle
    const auto start = std::chrono::steady_clock::now();
    const u64 base_patches = st.patches.load(), base_deltas = st.deltas.load(), base_updates = st.updates_sent.load();
    st.steady = true;
    const double cpu0 = cpu_seconds();
    auto next_report = start + std::chrono::seconds(report);
    auto last = start;
    u64 last_deltas = 0, last_patches = 0, last_updates = 0;
    const double tick_s = 0.01;
    double update_budget = 0, join_budget = 0, query_budget = 0;
    std::size_t next_host = 0;
    while (!g_stop && std::chrono::steady_clock::now() - start < std::chrono::seconds(duration)) {
        const auto tick_start = std::chrono::steady_clock::now();
        update_budget += update_rate * hosts * tick_s;
        while (update_budget >= 1 && !host_states.empty()) {
            update_budget -= 1;
            auto& h = *host_states[next_host++ % host_states.size()];
            if (!h.ready) continue;
            h.players = (h.players + 1) % 1000;
            const u32 idx = static_cast<u32>((next_host - 1) % host_states.size());
            log.mark(idx, h.players, mono_ns());
            h.c->host_update(wire::HostUpdate{.players = h.players}, false);
            st.updates_sent.fetch_add(1, std::memory_order_relaxed);
        }
        join_budget += join_rate * tick_s;
        while (join_budget >= 1 && !browser_states.empty()) {
            join_budget -= 1;
            browser_states[rng() % browser_states.size()]->c->join(host_uuid(static_cast<u32>(rng() % hosts), run_id), std::nullopt);
        }
        query_budget += query_rate * tick_s;
        while (query_budget >= 1 && !browser_states.empty()) {
            query_budget -= 1;
            browser_states[rng() % browser_states.size()]->c->query(wire::ViewSpec{.sort = wire::Sort::name}, "load", 20);
        }
        if (tick_start >= next_report) {
            const double secs = std::chrono::duration<double>(tick_start - last).count();
            const u64 d = st.deltas.load(), p = st.patches.load(), u = st.updates_sent.load();
            std::printf(
                "[%5.0fs] conns=%llu closed=%llu updates/s=%.0f deltas/s=%.0f patches/s=%.0f snapshots=%llu errors=%llu "
                "latency(us) p50=%llu p90=%llu p99=%llu p99.9=%llu max~=%llu (n=%llu)\n",
                std::chrono::duration<double>(tick_start - start).count(), static_cast<unsigned long long>(st.connected.load()),
                static_cast<unsigned long long>(st.closed.load()), static_cast<double>(u - last_updates) / secs,
                static_cast<double>(d - last_deltas) / secs, static_cast<double>(p - last_patches) / secs,
                static_cast<unsigned long long>(st.snapshots.load()), static_cast<unsigned long long>(st.errors.load()),
                static_cast<unsigned long long>(st.latency.percentile(50)), static_cast<unsigned long long>(st.latency.percentile(90)),
                static_cast<unsigned long long>(st.latency.percentile(99)), static_cast<unsigned long long>(st.latency.percentile(99.9)),
                static_cast<unsigned long long>(st.latency.percentile(100)), static_cast<unsigned long long>(st.latency.count()));
            std::fflush(stdout);
            last = tick_start;
            last_deltas = d;
            last_patches = p;
            last_updates = u;
            st.latency.reset();
            next_report = tick_start + std::chrono::seconds(report);
        }
        std::this_thread::sleep_until(tick_start + std::chrono::milliseconds(10));
    }

    st.steady = false;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double patches_s = static_cast<double>(st.patches.load() - base_patches) / secs;
    const double deltas_s = static_cast<double>(st.deltas.load() - base_deltas) / secs;
    const double updates_s = static_cast<double>(st.updates_sent.load() - base_updates) / secs;
    const double own_cpu = (cpu_seconds() - cpu0) / secs;
    auto pct = [&](double p) { return static_cast<unsigned long long>(st.total.percentile(p)); };
    std::printf("steady state %.0fs: updates/s=%.0f deltas/s=%.0f patches/s=%.0f latency(us) p50=%llu p90=%llu p99=%llu p99.9=%llu\n",
                secs, updates_s, deltas_s, patches_s, pct(50), pct(90), pct(99), pct(99.9));
    if (!summary_path.empty()) {
        if (FILE* f = std::fopen(summary_path.c_str(), "w")) {
            std::fprintf(f,
                         "{\"label\":\"%s\",\"hosts\":%u,\"browsers\":%u,\"subs\":%u,\"window\":%u,\"update_rate\":%.3f,"
                         "\"connected\":%llu,\"registered\":%llu,\"errors\":%llu,\"closed\":%llu,\"seconds\":%.1f,"
                         "\"updates_per_s\":%.0f,\"deltas_per_s\":%.0f,\"patches_per_s\":%.0f,\"snapshots\":%llu,"
                         "\"loadgen_cpu_cores\":%.2f,\"latency_us\":{\"samples\":%llu,\"p50\":%llu,\"p90\":%llu,\"p99\":%llu,\"p999\":%llu,\"max\":%llu}}\n",
                         label.c_str(), hosts, browsers, subs, window, update_rate,
                         static_cast<unsigned long long>(st.connected.load()), static_cast<unsigned long long>(st.registered.load()),
                         static_cast<unsigned long long>(st.errors.load()), static_cast<unsigned long long>(st.closed.load()), secs,
                         updates_s, deltas_s, patches_s, static_cast<unsigned long long>(st.snapshots.load()), own_cpu,
                         static_cast<unsigned long long>(st.total.count()), pct(50), pct(90), pct(99), pct(99.9), pct(100));
            std::fclose(f);
        }
    }

    std::printf("closing %zu connections\n", host_states.size() + browser_states.size());
    std::fflush(stdout);
    heartbeats = {};
    for (auto& b : browser_states) b->c->close();
    for (auto& h : host_states) {
        h->c->host_unregister();
        h->c->close();
    }
    // Give the close frames a moment to leave, then exit without tearing down tens of thousands of
    // clients one by one; the edge reaps anything that did not hear the close by idle timeout.
    std::this_thread::sleep_for(2s);
    std::fflush(nullptr);
    std::_Exit(st.errors.load() > 0 ? 3 : 0);
}
