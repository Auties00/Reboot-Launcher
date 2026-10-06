#pragma once

#include <string>
#include <vector>

#include "core/types.hpp"
#include "registry/replica.hpp"

namespace sb::edge {

struct TlsConfig {
    std::string cert_file;
    std::string key_file;
    std::string ticket_key_file;  // 44 bytes shared by all edges: enables resumption across edges
    std::string retry_key_file;   // 32 bytes shared by all edges: stateless retry validation
    bool self_signed = false;     // development only: generate a throwaway certificate
};

struct SecretsConfig {
    std::string pepper_file;  // password HMAC key; never leaves the edges
    std::string ticket_file;  // join ticket HMAC key
};

struct LimitsConfig {
    double conn_per_ip_per_sec = 10;
    u32 conn_per_ip_burst = 40;
    double conn_per_subnet_per_sec = 100;
    u32 conn_per_subnet_burst = 400;
    double query_per_sec = 10;
    u32 query_burst = 20;
    double join_per_min = 5;
    u32 join_burst = 5;
    double host_update_per_sec = 4;
    u32 host_update_burst = 8;
    u32 max_subscriptions = 8;
    u32 max_unsent_datagrams = 4;
    u32 max_stream_inflight = 64;
    u32 hello_timeout_ms = 10'000;
    u32 stall_timeout_ms = 30'000;
    u32 max_control_backlog = 256 * 1024;
    u32 max_frame = 16 * 1024;
};

struct QuicTuning {
    u32 idle_timeout_ms = 60'000;
    u32 host_idle_timeout_ms = 10'000;
    u32 host_keepalive_ms = 3'000;
    u32 initial_window_packets = 20;
    u32 max_worker_queue_delay_us = 20'000;
    u32 stream_recv_window = 16 * 1024;
    u16 peer_bidi_streams = 1;
    u16 peer_unidi_streams = 0;
    u16 retry_memory_percent = 20;
    bool ecn = true;
    bool xdp = false;
};

struct BackboneConfig {
    std::string kind = "inproc";  // inproc | nats
    std::vector<std::string> urls;
    std::string stream = "SB_REG";
    std::string subject_prefix = "sb";  // subjects: <prefix>.reg.<id> and <prefix>.live.<id>
    std::string lease_bucket = "SB_EDGES";
    std::string tls_ca;
    std::string tls_cert;
    std::string tls_key;
    u32 replicas = 3;
    u32 lease_ttl_ms = 10'000;
};

struct EdgeConfig {
    u64 edge_id = 0;  // 0: derived from the hostname
    std::string listen = "[::]:443";
    u32 shards = 0;  // 0: one per core minus two
    bool pin_shards = true;  // pin shard i to CPU cpu_offset + i
    u32 cpu_offset = 0;
    u32 search_workers = 2;
    u32 drain_delay_ms = 10'000;
    u32 drain_spread_ms = 30'000;
    std::string admin_listen = "127.0.0.1:9100";
    std::string geoip_db;  // optional MaxMind country database for region tagging
    std::string log_level = "info";
    bool log_json = true;

    TlsConfig tls;
    SecretsConfig secrets;
    LimitsConfig limits;
    QuicTuning quic;
    BackboneConfig backbone;
    registry::ReplicaConfig registry;
};

// Loads TOML (all keys optional) and validates the result; throws std::runtime_error.
[[nodiscard]] EdgeConfig load_config(const std::string& path);
void validate(const EdgeConfig& cfg);

}  // namespace sb::edge
