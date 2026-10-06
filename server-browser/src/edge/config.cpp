#include "edge/config.hpp"

#include <toml++/toml.hpp>

#include <stdexcept>

namespace sb::edge {

namespace {

template <class T>
void read(const toml::table& t, std::string_view path, T& out) {
    auto node = t.at_path(path);
    if (!node) return;
    if constexpr (std::is_same_v<T, bool>) {
        if (auto v = node.value<bool>()) out = *v;
        else throw std::runtime_error(std::string(path) + ": expected a boolean");
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (auto v = node.value<std::string>()) out = *v;
        else throw std::runtime_error(std::string(path) + ": expected a string");
    } else if constexpr (std::is_floating_point_v<T>) {
        if (auto v = node.value<double>()) out = static_cast<T>(*v);
        else throw std::runtime_error(std::string(path) + ": expected a number");
    } else {
        auto v = node.value<std::int64_t>();
        if (!v || *v < 0 || static_cast<std::uint64_t>(*v) > std::numeric_limits<T>::max())
            throw std::runtime_error(std::string(path) + ": expected a non-negative integer in range");
        out = static_cast<T>(*v);
    }
}

void read_list(const toml::table& t, std::string_view path, std::vector<std::string>& out) {
    auto node = t.at_path(path);
    if (!node) return;
    const auto* arr = node.as_array();
    if (!arr) throw std::runtime_error(std::string(path) + ": expected an array");
    out.clear();
    for (const auto& e : *arr) {
        auto v = e.value<std::string>();
        if (!v) throw std::runtime_error(std::string(path) + ": expected strings");
        out.push_back(*v);
    }
}

void read_list(const toml::table& t, std::string_view path, std::vector<u32>& out) {
    auto node = t.at_path(path);
    if (!node) return;
    const auto* arr = node.as_array();
    if (!arr) throw std::runtime_error(std::string(path) + ": expected an array");
    out.clear();
    for (const auto& e : *arr) {
        auto v = e.value<std::int64_t>();
        if (!v || *v <= 0) throw std::runtime_error(std::string(path) + ": expected positive integers");
        out.push_back(static_cast<u32>(*v));
    }
}

}  // namespace

EdgeConfig load_config(const std::string& path) {
    EdgeConfig c;
    if (path.empty()) return c;
    toml::table t;
    try {
        t = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error("config " + path + ": " + std::string(e.description()));
    }

    read(t, "edge.id", c.edge_id);
    read(t, "edge.listen", c.listen);
    read(t, "edge.shards", c.shards);
    read(t, "edge.pin_shards", c.pin_shards);
    read(t, "edge.cpu_offset", c.cpu_offset);
    read(t, "edge.search_workers", c.search_workers);
    read(t, "edge.drain_delay_ms", c.drain_delay_ms);
    read(t, "edge.drain_spread_ms", c.drain_spread_ms);
    read(t, "edge.geoip_db", c.geoip_db);
    read(t, "admin.listen", c.admin_listen);
    read(t, "log.level", c.log_level);
    read(t, "log.json", c.log_json);

    read(t, "tls.cert", c.tls.cert_file);
    read(t, "tls.key", c.tls.key_file);
    read(t, "tls.ticket_key_file", c.tls.ticket_key_file);
    read(t, "tls.retry_key_file", c.tls.retry_key_file);
    read(t, "tls.self_signed", c.tls.self_signed);
    read(t, "secrets.pepper_file", c.secrets.pepper_file);
    read(t, "secrets.ticket_file", c.secrets.ticket_file);

    auto& l = c.limits;
    read(t, "limits.conn_per_ip_per_sec", l.conn_per_ip_per_sec);
    read(t, "limits.conn_per_ip_burst", l.conn_per_ip_burst);
    read(t, "limits.conn_per_subnet_per_sec", l.conn_per_subnet_per_sec);
    read(t, "limits.conn_per_subnet_burst", l.conn_per_subnet_burst);
    read(t, "limits.query_per_sec", l.query_per_sec);
    read(t, "limits.query_burst", l.query_burst);
    read(t, "limits.join_per_min", l.join_per_min);
    read(t, "limits.join_burst", l.join_burst);
    read(t, "limits.host_update_per_sec", l.host_update_per_sec);
    read(t, "limits.host_update_burst", l.host_update_burst);
    read(t, "limits.max_subscriptions", l.max_subscriptions);
    read(t, "limits.max_unsent_datagrams", l.max_unsent_datagrams);
    read(t, "limits.max_stream_inflight", l.max_stream_inflight);
    read(t, "limits.hello_timeout_ms", l.hello_timeout_ms);
    read(t, "limits.stall_timeout_ms", l.stall_timeout_ms);
    read(t, "limits.max_control_backlog", l.max_control_backlog);
    read(t, "limits.max_frame", l.max_frame);

    auto& q = c.quic;
    read(t, "quic.idle_timeout_ms", q.idle_timeout_ms);
    read(t, "quic.host_keepalive_ms", q.host_keepalive_ms);
    read(t, "quic.initial_window_packets", q.initial_window_packets);
    read(t, "quic.max_worker_queue_delay_us", q.max_worker_queue_delay_us);
    read(t, "quic.stream_recv_window", q.stream_recv_window);
    read(t, "quic.retry_memory_percent", q.retry_memory_percent);
    read(t, "quic.ecn", q.ecn);
    read(t, "quic.xdp", q.xdp);

    auto& b = c.backbone;
    read(t, "backbone.kind", b.kind);
    read_list(t, "backbone.urls", b.urls);
    read(t, "backbone.stream", b.stream);
    read(t, "backbone.subject_prefix", b.subject_prefix);
    read(t, "backbone.lease_bucket", b.lease_bucket);
    read(t, "backbone.tls_ca", b.tls_ca);
    read(t, "backbone.tls_cert", b.tls_cert);
    read(t, "backbone.tls_key", b.tls_key);
    read(t, "backbone.replicas", b.replicas);
    read(t, "backbone.lease_ttl_ms", b.lease_ttl_ms);

    auto& r = c.registry;
    read(t, "registry.max_entries", r.max_entries);
    read(t, "registry.ring_capacity", r.ring_capacity);
    read(t, "registry.heartbeat_ms", r.heartbeat_ms);
    read(t, "registry.ttl_ms", r.ttl_ms);
    read(t, "registry.grace_ms", r.grace_ms);
    read(t, "registry.persist_interval_ms", r.persist_interval_ms);
    read(t, "registry.probe_interval_ms", r.probe_interval_ms);
    read(t, "registry.max_hosts_per_ip", r.max_hosts_per_ip);
    read(t, "registry.probe", r.probe_enabled);
    read(t, "registry.zstd", r.zstd);
    read_list(t, "registry.windows", r.windows);
    read(t, "registry.max_query_limit", r.max_query_limit);
    return c;
}

void validate(const EdgeConfig& c) {
    auto fail = [](const std::string& m) { throw std::runtime_error("config: " + m); };
    if (c.registry.windows.empty()) fail("registry.windows must not be empty");
    for (u32 w : c.registry.windows)
        if (w == 0 || w > 1000) fail("registry.windows entries must be in 1..1000");
    if (c.registry.max_entries < 2 || c.registry.max_entries > (1u << 26)) fail("registry.max_entries out of range");
    if ((c.registry.ring_capacity & (c.registry.ring_capacity - 1)) != 0) fail("registry.ring_capacity must be a power of two");
    if (c.registry.ttl_ms <= c.registry.heartbeat_ms) fail("registry.ttl_ms must exceed heartbeat_ms");
    if (c.limits.max_subscriptions == 0 || c.limits.max_subscriptions > 16) fail("limits.max_subscriptions must be 1..16");
    if (c.limits.max_unsent_datagrams == 0) fail("limits.max_unsent_datagrams must be positive");
    if (c.backbone.kind != "inproc" && c.backbone.kind != "nats") fail("backbone.kind must be inproc or nats");
    if (c.backbone.kind == "nats" && c.backbone.urls.empty()) fail("backbone.urls required for nats");
    if (!c.tls.self_signed && (c.tls.cert_file.empty() || c.tls.key_file.empty()))
        fail("tls.cert and tls.key are required unless tls.self_signed = true");
    if (c.secrets.pepper_file.empty() && !c.tls.self_signed)
        fail("secrets.pepper_file is required in production (tls.self_signed = false)");
}

}  // namespace sb::edge
