#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "backbone/backbone.hpp"
#include "edge/config.hpp"
#include "edge/geoip.hpp"
#include "edge/prober.hpp"
#include "edge/shard.hpp"
#include "ops/admin_http.hpp"
#include "quic/msquic.hpp"
#include "registry/replica.hpp"
#include "registry/search_worker.hpp"

namespace sb::edge {

// One edge process: QUIC listener, shards (one per core), the replica, search workers, the
// reachability prober, the backbone connection and the admin endpoint.
class Edge {
public:
    explicit Edge(EdgeConfig cfg);
    ~Edge();
    Edge(const Edge&) = delete;
    Edge& operator=(const Edge&) = delete;

    void start();
    // Graceful: leave DNS rotation, GoAway every client, wait for them to move, then stop.
    void drain();
    void stop();
    // SIGHUP: fresh certificate and GeoIP database for new connections; nothing is dropped.
    void reload();

    [[nodiscard]] ops::HttpResponse admin(std::string_view path);
    [[nodiscard]] std::string metrics();
    [[nodiscard]] u16 port() const noexcept { return port_; }
    [[nodiscard]] u16 admin_port() const noexcept { return admin_ ? static_cast<u16>(admin_->port()) : 0; }
    [[nodiscard]] const EdgeConfig& config() const noexcept { return cfg_; }

private:
    void open_quic();
    HQUIC make_configuration();
    void load_credentials(HQUIC configuration);
    void start_listener();
    std::vector<u8> read_secret(const std::string& path, std::size_t min_len, const char* what);

    EdgeConfig cfg_;
    std::unique_ptr<quic::Library> lib_;
    const QUIC_API_TABLE* api_ = nullptr;
    HQUIC registration_ = nullptr;
    HQUIC configuration_ = nullptr;
    HQUIC listener_ = nullptr;
    std::vector<QUIC_EXECUTION*> execs_;
    u16 port_ = 0;

    std::unique_ptr<backbone::Backbone> backbone_;
    std::unique_ptr<registry::Replica> replica_;
    std::unique_ptr<EdgeContext> ctx_;
    std::vector<std::unique_ptr<Shard>> shards_;
    std::vector<Shard*> shard_ptrs_;
    std::vector<std::unique_ptr<registry::SearchWorker>> search_;
    std::unique_ptr<Prober> prober_;
    std::unique_ptr<ops::AdminServer> admin_;

    std::jthread replica_thread_;
    std::vector<std::jthread> search_threads_;
    std::vector<std::jthread> shard_threads_;
    std::jthread prober_thread_;
    std::jthread admin_thread_;
    std::jthread lease_thread_;

    std::atomic<bool> ready_{false};
    std::atomic<bool> running_{false};
    std::string dev_cert_dir_;
};

}  // namespace sb::edge
