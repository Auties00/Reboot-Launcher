#include <CLI/CLI.hpp>

#include <csignal>
#include <cstdio>
#include <pthread.h>

#include "edge/config.hpp"
#include "edge/edge.hpp"
#include "ops/log.hpp"
#include "ops/sd_notify.hpp"

int main(int argc, char** argv) {
    CLI::App app{"sb-edge: real-time QUIC server browser edge"};
    std::string config_path, listen, admin, backbone, log_level;
    std::vector<std::string> nats;
    unsigned shards = 0, search = 0;
    unsigned long long edge_id = 0;
    bool self_signed = false, no_probe = false, plain_logs = false;
    app.add_option("-c,--config", config_path, "TOML configuration file");
    app.add_option("--listen", listen, "QUIC listen address, e.g. [::]:443");
    app.add_option("--admin", admin, "metrics/health listen address");
    app.add_option("--shards", shards, "shard threads (0: cores - 2)");
    app.add_option("--search-workers", search, "text search threads");
    app.add_option("--backbone", backbone, "inproc | nats");
    app.add_option("--nats", nats, "NATS server URLs");
    app.add_option("--edge-id", edge_id, "unique edge id (0: derive from hostname)");
    app.add_option("--log-level", log_level, "debug | info | warn | error");
    app.add_flag("--self-signed", self_signed, "generate a development certificate");
    app.add_flag("--no-probe", no_probe, "skip UDP reachability probes (development)");
    app.add_flag("--plain-logs", plain_logs, "human-readable logs instead of JSON");
    CLI11_PARSE(app, argc, argv);

    // Signals are handled synchronously by this thread; every other thread inherits the mask.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        sb::edge::EdgeConfig cfg = sb::edge::load_config(config_path);
        if (!listen.empty()) cfg.listen = listen;
        if (!admin.empty()) cfg.admin_listen = admin;
        if (shards) cfg.shards = shards;
        if (search) cfg.search_workers = search;
        if (!backbone.empty()) cfg.backbone.kind = backbone;
        if (!nats.empty()) cfg.backbone.urls = nats;
        if (edge_id) cfg.edge_id = edge_id;
        if (!log_level.empty()) cfg.log_level = log_level;
        if (self_signed) cfg.tls.self_signed = true;
        if (no_probe) cfg.registry.probe_enabled = false;
        if (plain_logs) cfg.log_json = false;

        sb::edge::Edge edge(std::move(cfg));
        edge.start();
        sb::ops::sd_notify("READY=1");
        for (;;) {
            int sig = 0;
            sigwait(&set, &sig);
            if (sig == SIGHUP) {
                sb::ops::sd_notify("RELOADING=1");
                edge.reload_certificate();
                sb::ops::sd_notify("READY=1");
                continue;
            }
            sb::ops::sd_notify("STOPPING=1");
            if (sig == SIGTERM) edge.drain();
            else edge.stop();
            break;
        }
    } catch (const std::exception& e) {
        sb::log::error("fatal: {}", e.what());
        std::fprintf(stderr, "sb-edge: %s\n", e.what());
        return 1;
    }
    return 0;
}
