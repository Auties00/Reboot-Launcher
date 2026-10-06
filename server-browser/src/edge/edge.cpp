#include "edge/edge.hpp"

#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>

#include "backbone/factory.hpp"
#include "core/hash.hpp"
#include "core/time.hpp"
#include "ops/log.hpp"
#include "security/selfsigned.hpp"

namespace sb::edge {

namespace {

QUIC_STATUS QUIC_API listener_cb(HQUIC, void* ctx, QUIC_LISTENER_EVENT* ev) {
    if (ev->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION) return QUIC_STATUS_SUCCESS;
    // The listener callback runs on the worker that will own the connection, i.e. a shard thread.
    Shard* s = Shard::current;
    if (!s) return QUIC_STATUS_CONNECTION_REFUSED;
    (void)ctx;
    return s->accept(ev->NEW_CONNECTION.Connection, *ev->NEW_CONNECTION.Info);
}

u64 derive_edge_id(const std::string& listen) {
    char host[256] = {};
    ::gethostname(host, sizeof(host) - 1);
    const std::string key = std::string(host) + "|" + listen;
    const u64 h = hash_bytes(key.data(), key.size());
    return h ? h : 1;
}

u32 default_shards() {
    const unsigned n = std::thread::hardware_concurrency();
    return n > 3 ? n - 2 : 1;
}

}  // namespace

Edge::Edge(EdgeConfig cfg) : cfg_(std::move(cfg)) {
    validate(cfg_);
    if (cfg_.edge_id == 0) cfg_.edge_id = derive_edge_id(cfg_.listen);
    if (cfg_.shards == 0) cfg_.shards = default_shards();
    cfg_.registry.edge_id = cfg_.edge_id;
    cfg_.registry.num_shards = cfg_.shards;
}

Edge::~Edge() { stop(); }

std::vector<u8> Edge::read_secret(const std::string& path, std::size_t min_len, const char* what) {
    if (path.empty()) {
        log::warn("no {} configured: using a random per-process secret (development only)", what);
        std::vector<u8> v(32);
        security::random_bytes(v);
        return v;
    }
    std::ifstream f(path, std::ios::binary);
    std::vector<u8> v{std::istreambuf_iterator<char>(f), {}};
    if (v.size() < min_len) throw std::runtime_error(std::string(what) + " in " + path + " is shorter than " + std::to_string(min_len) + " bytes");
    return v;
}

void Edge::start() {
    log::init(log::parse_level(cfg_.log_level), cfg_.log_json);
    log::info("edge {:016x} starting: {} shards, {} search workers, backbone {}", cfg_.edge_id, cfg_.shards, cfg_.search_workers, cfg_.backbone.kind);

    lib_ = std::make_unique<quic::Library>();
    api_ = lib_->get();

    backbone_ = backbone::make_backbone(cfg_.backbone, cfg_.edge_id);
    replica_ = std::make_unique<registry::Replica>(cfg_.registry, *backbone_);
    ctx_ = std::make_unique<EdgeContext>(cfg_, *replica_);
    ctx_->api = api_;
    if (!cfg_.geoip_db.empty()) {
        ctx_->geoip.store(std::make_shared<const GeoIp>(cfg_.geoip_db));
        log::info("region tagging from {}", cfg_.geoip_db);
    }
    const auto pepper = read_secret(cfg_.secrets.pepper_file, 32, "password pepper");
    std::copy_n(security::sha256(pepper).begin(), 32, ctx_->pepper.begin());
    ctx_->ticket_key = read_secret(cfg_.secrets.ticket_file, 32, "join ticket key");

    // Ring consumers must all exist before the replica publishes anything.
    for (u16 i = 0; i < cfg_.shards; ++i) {
        shards_.push_back(std::make_unique<Shard>(*ctx_, i, shard_ptrs_));
        shard_ptrs_.push_back(shards_.back().get());
    }
    std::vector<Mailbox*> boxes;
    for (auto& s : shards_) boxes.push_back(&s->mailbox());
    for (u32 i = 0; i < cfg_.search_workers; ++i)
        search_.push_back(std::make_unique<registry::SearchWorker>(*replica_, boxes, cfg_.registry.max_query_limit));
    for (auto& w : search_) ctx_->search.push_back(w.get());

    prober_ = std::make_unique<Prober>(*replica_, ProberConfig{});
    replica_->set_probe([p = prober_.get()](u32 h, const IpAddr& a, u16 port) { p->request(h, a, port); });

    open_quic();

    replica_thread_ = std::jthread([this](std::stop_token st) { replica_->run(st); });
    for (auto& w : search_) search_threads_.emplace_back([w = w.get()](std::stop_token st) { w->run(st); });
    prober_thread_ = std::jthread([this](std::stop_token st) { prober_->run(st); });
    for (auto& s : shards_) shard_threads_.emplace_back([s = s.get()](std::stop_token st) { s->run(st); });
    if (backbone_->clustered())
        lease_thread_ = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                backbone_->heartbeat_lease(cfg_.edge_id);
                // Renew well within the lease TTL so a live edge never looks dead.
                const auto every = std::chrono::milliseconds(std::max<u32>(100, cfg_.backbone.lease_ttl_ms / 3));
                const auto until = std::chrono::steady_clock::now() + every;
                while (!st.stop_requested() && std::chrono::steady_clock::now() < until)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

    // Serve only once the registry replay from the backbone is complete.
    const auto replay_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!backbone_->caught_up() && std::chrono::steady_clock::now() < replay_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (!backbone_->caught_up()) log::warn("backbone replay still in progress; serving a partial registry");

    start_listener();

    admin_ = std::make_unique<ops::AdminServer>(cfg_.admin_listen, [this](std::string_view p) { return admin(p); });
    admin_thread_ = std::jthread([this](std::stop_token st) { admin_->run(st); });

    running_ = true;
    ready_ = true;
    log::info("edge listening on {} (udp port {}), admin on {}", cfg_.listen, port_, cfg_.admin_listen);
}

void Edge::open_quic() {
    const auto& q = cfg_.quic;
    u16 retry_pct = q.retry_memory_percent;
    quic::check(api_->SetParam(nullptr, QUIC_PARAM_GLOBAL_RETRY_MEMORY_PERCENT, sizeof(retry_pct), &retry_pct), "retry memory");
    if (!cfg_.tls.retry_key_file.empty()) {
        const auto secret = read_secret(cfg_.tls.retry_key_file, 32, "retry key");
        QUIC_STATELESS_RETRY_CONFIG rc{QUIC_AEAD_ALGORITHM_AES_256_GCM, 30u * 60 * 1000, 32, secret.data()};
        quic::check(api_->SetParam(nullptr, QUIC_PARAM_GLOBAL_STATELESS_RETRY_CONFIG, sizeof(rc), &rc), "stateless retry config");
    }

    // One app-owned execution context per shard: MsQuic runs inside the shard loops.
    std::vector<QUIC_EXECUTION_CONFIG> ecfg(cfg_.shards);
    for (u16 i = 0; i < cfg_.shards; ++i) shards_[i]->bind_execution(ecfg[i]);
    execs_.resize(cfg_.shards);
    quic::check(api_->ExecutionCreate(QUIC_GLOBAL_EXECUTION_CONFIG_FLAG_NONE, 0, cfg_.shards, ecfg.data(), execs_.data()),
                "ExecutionCreate (MsQuic >= 2.5 with app-owned execution required)");
    for (u16 i = 0; i < cfg_.shards; ++i) shards_[i]->set_execution(execs_[i]);

    const QUIC_REGISTRATION_CONFIG reg{"server-browser", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
    quic::check(api_->RegistrationOpen(&reg, &registration_), "RegistrationOpen");
    configuration_ = make_configuration();
    ctx_->configuration.store(configuration_);
}

HQUIC Edge::make_configuration() {
    const auto& q = cfg_.quic;
    QUIC_SETTINGS s{};
    s.IsSet.IdleTimeoutMs = 1;
    s.IdleTimeoutMs = q.idle_timeout_ms;
    s.IsSet.ServerResumptionLevel = 1;
    s.ServerResumptionLevel = QUIC_SERVER_RESUME_ONLY;  // resumption without 0-RTT
    s.IsSet.PeerBidiStreamCount = 1;
    s.PeerBidiStreamCount = q.peer_bidi_streams;
    s.IsSet.PeerUnidiStreamCount = 1;
    s.PeerUnidiStreamCount = q.peer_unidi_streams;
    s.IsSet.DatagramReceiveEnabled = 1;
    s.DatagramReceiveEnabled = 1;
    s.IsSet.SendBufferingEnabled = 1;
    s.SendBufferingEnabled = 0;  // shared frames stay ours until completion: no per-send copy
    s.IsSet.PacingEnabled = 1;
    s.PacingEnabled = 1;
    s.IsSet.CongestionControlAlgorithm = 1;
    s.CongestionControlAlgorithm = QUIC_CONGESTION_CONTROL_ALGORITHM_CUBIC;
    s.IsSet.HyStartEnabled = 1;
    s.HyStartEnabled = 1;
    s.IsSet.InitialWindowPackets = 1;
    s.InitialWindowPackets = q.initial_window_packets;
    s.IsSet.EcnEnabled = 1;
    s.EcnEnabled = q.ecn;
    s.IsSet.MaxWorkerQueueDelayUs = 1;
    s.MaxWorkerQueueDelayUs = q.max_worker_queue_delay_us;
    s.IsSet.StreamRecvWindowDefault = 1;
    s.StreamRecvWindowDefault = q.stream_recv_window;
    s.IsSet.MigrationEnabled = 1;
    s.MigrationEnabled = 0;  // keeps every connection on its shard
    const QUIC_BUFFER alpn{static_cast<u32>(wire::kAlpn.size()), reinterpret_cast<u8*>(const_cast<char*>(wire::kAlpn.data()))};
    HQUIC conf = nullptr;
    quic::check(api_->ConfigurationOpen(registration_, &alpn, 1, &s, sizeof(s), nullptr, &conf), "ConfigurationOpen");
    try {
        load_credentials(conf);
    } catch (...) {
        api_->ConfigurationClose(conf);
        throw;
    }
    return conf;
}

void Edge::load_credentials(HQUIC configuration) {
    std::string cert = cfg_.tls.cert_file, key = cfg_.tls.key_file;
    if (cfg_.tls.self_signed) {
        log::warn("using a self-signed certificate (development only)");
        auto pem = security::write_self_signed("/tmp");
        cert = pem.cert;
        key = pem.key;
    }
    QUIC_CERTIFICATE_FILE cf{key.c_str(), cert.c_str()};
    QUIC_CREDENTIAL_CONFIG cred{};
    cred.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    cred.Flags = QUIC_CREDENTIAL_FLAG_NONE;
    cred.CertificateFile = &cf;
    quic::check(api_->ConfigurationLoadCredential(configuration, &cred), "ConfigurationLoadCredential");

    if (!cfg_.tls.ticket_key_file.empty()) {
        const auto material = read_secret(cfg_.tls.ticket_key_file, 48, "ticket key");
        QUIC_TICKET_KEY_CONFIG tk{};
        std::copy_n(material.begin(), 16, tk.Id);
        tk.MaterialLength = static_cast<u8>(std::min<std::size_t>(64, material.size() - 16));
        std::copy_n(material.begin() + 16, tk.MaterialLength, tk.Material);
        quic::check(api_->SetParam(configuration, QUIC_PARAM_CONFIGURATION_TICKET_KEYS, sizeof(tk), &tk), "ticket keys");
    }
}

void Edge::start_listener() {
    auto [host, port] = ops::split_host_port(cfg_.listen);
    QUIC_ADDR addr{};
    if (host.empty() || host == "::" || host == "0.0.0.0") {
        QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
    } else if (!QuicAddrFromString(host.c_str(), static_cast<u16>(port), &addr)) {
        throw std::runtime_error("invalid listen address " + cfg_.listen);
    }
    QuicAddrSetPort(&addr, static_cast<u16>(port));
    quic::check(api_->ListenerOpen(registration_, &listener_cb, this, &listener_), "ListenerOpen");
    const QUIC_BUFFER alpn{static_cast<u32>(wire::kAlpn.size()), reinterpret_cast<u8*>(const_cast<char*>(wire::kAlpn.data()))};
    quic::check(api_->ListenerStart(listener_, &alpn, 1, &addr), "ListenerStart");
    QUIC_ADDR bound{};
    u32 len = sizeof(bound);
    if (QUIC_SUCCEEDED(api_->GetParam(listener_, QUIC_PARAM_LISTENER_LOCAL_ADDRESS, &len, &bound))) port_ = QuicAddrGetPort(&bound);
}

void Edge::reload() {
    if (!cfg_.geoip_db.empty()) {
        try {
            ctx_->geoip.store(std::make_shared<const GeoIp>(cfg_.geoip_db));
            log::info("GeoIP database reloaded");
        } catch (const std::exception& e) {
            log::error("GeoIP reload failed, keeping the current database: {}", e.what());
        }
    }
    // New connections pick up the fresh configuration; existing ones keep the one they started with.
    try {
        HQUIC fresh = make_configuration();
        HQUIC old = ctx_->configuration.exchange(fresh);
        configuration_ = fresh;
        if (old) api_->ConfigurationClose(old);
        log::info("certificate reloaded");
    } catch (const std::exception& e) {
        log::error("certificate reload failed, keeping the current one: {}", e.what());
    }
}

void Edge::drain() {
    if (!running_.exchange(false)) return;
    log::info("draining: leaving rotation for {} ms", cfg_.drain_delay_ms);
    ready_ = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.drain_delay_ms));
    ctx_->draining = true;
    replica_->post(registry::kNoShard, 0, registry::DrainReq{.phase = 0});
    log::info("draining: GoAway sent, waiting {} ms for clients to move", cfg_.drain_spread_ms);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg_.drain_spread_ms + 2000);
    while (std::chrono::steady_clock::now() < deadline) {
        std::size_t n = 0;
        for (auto& s : shards_) n += s->connections();
        if (n == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    running_ = true;  // let stop() run the teardown
    stop();
}

void Edge::stop() {
    if (!api_) return;
    ready_ = false;
    running_ = false;
    if (ctx_) ctx_->draining = true;
    if (listener_) {
        api_->ListenerStop(listener_);
        api_->ListenerClose(listener_);
        listener_ = nullptr;
    }
    if (replica_) replica_->post(registry::kNoShard, 0, registry::DrainReq{.phase = 1});
    // Shards keep polling MsQuic until their connections finish closing.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        std::size_t n = 0;
        for (auto& s : shards_) n += s->connections();
        if (n == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (configuration_) {
        api_->ConfigurationClose(configuration_);
        configuration_ = nullptr;
    }
    if (registration_) {
        api_->RegistrationClose(registration_);  // blocks until MsQuic released everything
        registration_ = nullptr;
    }
    for (auto& t : shard_threads_) t.request_stop();
    shard_threads_.clear();
    if (!execs_.empty()) {
        api_->ExecutionDelete(static_cast<u32>(execs_.size()), execs_.data());
        execs_.clear();
    }
    admin_thread_ = {};
    lease_thread_ = {};
    prober_thread_ = {};
    search_threads_.clear();
    replica_thread_ = {};
    if (backbone_) backbone_->stop();
    shards_.clear();
    search_.clear();
    prober_.reset();
    replica_.reset();
    backbone_.reset();
    lib_.reset();
    api_ = nullptr;
    log::info("edge stopped");
}

namespace {

// Resident set size and CPU time of this process, for capacity planning.
std::pair<u64, double> process_usage() {
    u64 rss = 0;
    double cpu = 0;
    if (std::ifstream st("/proc/self/statm"); st) {
        u64 pages = 0;
        st >> pages >> pages;
        rss = pages * static_cast<u64>(::sysconf(_SC_PAGESIZE));
    }
    if (std::ifstream st("/proc/self/stat"); st) {
        std::string line;
        std::getline(st, line);
        // Fields after the parenthesised command name; utime and stime are fields 14 and 15.
        std::istringstream in(line.substr(line.rfind(')') + 2));
        std::string field;
        u64 utime = 0, stime = 0;
        for (int i = 3; i <= 15 && in >> field; ++i) {
            if (i == 14) utime = std::stoull(field);
            if (i == 15) stime = std::stoull(field);
        }
        cpu = static_cast<double>(utime + stime) / static_cast<double>(::sysconf(_SC_CLK_TCK));
    }
    return {rss, cpu};
}

}  // namespace

std::string Edge::metrics() {
    ops::Exposition x;
    const auto [rss, cpu] = process_usage();
    x.family("sb_process_resident_bytes", "gauge", "Resident set size");
    x.sample("sb_process_resident_bytes", "", rss);
    x.family("sb_process_cpu_seconds_total", "counter", "User plus system CPU time");
    x.sample("sb_process_cpu_seconds_total", "", cpu);
    auto shard_counter = [&](const char* name, const char* help, ops::Counter ShardStats::*field, const char* type = "counter") {
        x.family(name, type, help);
        for (auto& s : shards_) x.sample(name, "shard=\"" + std::to_string(s->index()) + "\"", (s->stats().*field).get());
    };
    shard_counter("sb_connections_browser", "Open browser connections", &ShardStats::conns_browser, "gauge");
    shard_counter("sb_connections_host", "Open host connections", &ShardStats::conns_host, "gauge");
    shard_counter("sb_connections_accepted_total", "Accepted connections", &ShardStats::accepted);
    shard_counter("sb_connections_rejected_total", "Connections refused by rate limits or drain", &ShardStats::rejected);
    shard_counter("sb_connections_closed_total", "Closed connections", &ShardStats::closed);
    shard_counter("sb_datagrams_direct_total", "Shared delta frames sent immediately", &ShardStats::dgram_direct);
    shard_counter("sb_datagrams_flush_total", "Coalesced datagrams built from current values", &ShardStats::dgram_flush);
    shard_counter("sb_datagrams_lost_total", "Datagrams suspected lost", &ShardStats::dgram_lost);
    shard_counter("sb_datagrams_failed_total", "Datagram sends refused by MsQuic", &ShardStats::dgram_failed);
    shard_counter("sb_dirty_marks_total", "Autocorked entry changes", &ShardStats::dirty_marks);
    shard_counter("sb_repairs_total", "Loss repairs scheduled", &ShardStats::repairs);
    shard_counter("sb_snapshots_total", "Snapshots sent", &ShardStats::snapshots);
    shard_counter("sb_window_syncs_total", "Lagging subscriptions caught up with a membership list", &ShardStats::window_syncs);
    shard_counter("sb_control_frames_in_total", "Control frames received", &ShardStats::ctrl_frames_in);
    shard_counter("sb_control_frames_out_total", "Control frames sent", &ShardStats::ctrl_frames_out);
    shard_counter("sb_rate_limited_total", "Requests rejected by rate limits", &ShardStats::rate_limited);
    shard_counter("sb_protocol_errors_total", "Connections closed for protocol violations", &ShardStats::protocol_errors);
    shard_counter("sb_stalled_total", "Connections closed for not draining sends", &ShardStats::stalls);
    shard_counter("sb_moved_connections_total", "Connections whose events crossed shards", &ShardStats::moved_conns);
    shard_counter("sb_ring_events_total", "Ring events consumed", &ShardStats::ring_events);

    std::vector<const ops::Histogram*> h;
    for (auto& s : shards_) h.push_back(&s->stats().delivery_us);
    x.family("sb_delivery_seconds", "histogram", "Replica mutation to datagram handoff latency");
    x.histogram("sb_delivery_seconds", "", h);

    auto& rs = replica_->stats();
    x.family("sb_entries", "gauge", "Entries in the registry");
    x.sample("sb_entries", "", rs.entries.load());
    x.family("sb_views", "gauge", "Materialized views");
    x.sample("sb_views", "", rs.views.load());
    x.family("sb_mutations_total", "counter", "Registry mutations applied");
    x.sample("sb_mutations_total", "", rs.mutations.load());
    x.family("sb_frames_total", "counter", "Delta frames encoded");
    x.sample("sb_frames_total", "", rs.frames.load());
    x.family("sb_ring_stalls_total", "counter", "Times the replica waited for a slow ring consumer");
    x.sample("sb_ring_stalls_total", "", rs.ring_stalls.load());
    x.family("sb_lifecycle_conflicts_total", "counter", "Compare-and-set conflicts on the lifecycle path");
    x.sample("sb_lifecycle_conflicts_total", "", rs.lifecycle_conflicts.load());
    x.family("sb_search_queries_total", "counter", "Text queries answered");
    for (std::size_t i = 0; i < search_.size(); ++i)
        x.sample("sb_search_queries_total", "worker=\"" + std::to_string(i) + "\"", search_[i]->queries());

    if (api_) {
        uint64_t perf[QUIC_PERF_COUNTER_MAX] = {};
        u32 len = sizeof(perf);
        if (QUIC_SUCCEEDED(api_->GetParam(nullptr, QUIC_PARAM_GLOBAL_PERF_COUNTERS, &len, perf))) {
            static constexpr std::pair<int, const char*> kPerf[] = {
                {QUIC_PERF_COUNTER_CONN_CREATED, "msquic_connections_created_total"},
                {QUIC_PERF_COUNTER_CONN_HANDSHAKE_FAIL, "msquic_handshake_failures_total"},
                {QUIC_PERF_COUNTER_CONN_ACTIVE, "msquic_connections_active"},
                {QUIC_PERF_COUNTER_CONN_QUEUE_DEPTH, "msquic_connection_queue_depth"},
                {QUIC_PERF_COUNTER_UDP_RECV, "msquic_udp_recv_total"},
                {QUIC_PERF_COUNTER_UDP_SEND, "msquic_udp_send_total"},
                {QUIC_PERF_COUNTER_PKTS_DROPPED, "msquic_packets_dropped_total"},
            };
            for (auto [idx, name] : kPerf) {
                x.family(name, "gauge", "MsQuic perf counter");
                x.sample(name, "", static_cast<u64>(perf[idx]));
            }
        }
    }
    return x.take();
}

ops::HttpResponse Edge::admin(std::string_view path) {
    if (path == "/metrics") return {200, "text/plain; version=0.0.4", metrics()};
    if (path == "/healthz") return {running_ || ready_ ? 200 : 503, "text/plain", running_ || ready_ ? "ok\n" : "stopping\n"};
    if (path == "/readyz") return {ready_ ? 200 : 503, "text/plain", ready_ ? "ready\n" : "draining\n"};
    return {404, "text/plain", "not found\n"};
}

}  // namespace sb::edge
