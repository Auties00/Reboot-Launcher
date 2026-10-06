#pragma once

// Cluster replication. Two paths (README.md, "Replication"):
//   lifecycle - durable, totally ordered, compare-and-set per entry (JetStream subject reg.e.<uuid>)
//   soft      - fire-and-forget fan-out of latest-value-wins changes (core NATS live.<uuid>)
// Every delivery lands in the replica inbox as a ReplicaMsg, including the edge's own lifecycle
// writes, so local and remote changes are applied by one code path.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/types.hpp"
#include "wire/codec.hpp"
#include "wire/messages.hpp"

namespace sb::registry {
struct ReplicaMsg;
}

namespace sb::backbone {

// Full entry as stored on the lifecycle path (protobuf-compatible encoding).
struct ReplicatedRecord {
    Uuid id;
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    u32 players = 0;
    u32 max_players = 0;
    wire::Region region{};
    bool hidden = false;
    bool reachable = false;
    bool online = false;
    u64 created_ms = 0;
    u64 updated_ms = 0;
    wire::Bytes addr;  // 16 bytes, IPv4-mapped for IPv4
    u32 port = 0;
    wire::Bytes password_mac;  // empty or 32 bytes
    wire::Bytes token_hash;    // 32 bytes
    u64 owner_edge = 0;
    u64 ver = 0;
};

// Latest-value-wins change published by the owning edge; applied only if `ver` is newer.
struct SoftUpdate {
    Uuid id;
    u64 ver = 0;
    u64 owner_edge = 0;
    u64 updated_ms = 0;
    std::optional<u32> players;
    std::optional<u32> max_players;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<bool> hidden;
    std::optional<bool> reachable;
    std::optional<bool> online;
};

// Receives deliveries; implementations push them into the replica inbox.
using Sink = std::function<void(registry::ReplicaMsg*)>;

class Backbone {
public:
    virtual ~Backbone() = default;

    virtual void start(Sink sink) = 0;
    virtual void stop() = 0;

    // Compare-and-set write of the entry's subject. `expected_seq` is the last stream sequence the
    // caller saw for this subject (0: must not exist). Empty payload writes a tombstone.
    // Completion arrives as a BackboneAck carrying `op`; the record itself as a BackboneRecord.
    virtual void publish_record(const Uuid& id, std::vector<u8> payload, u64 expected_seq, u64 op) = 0;

    // Low-latency, best-effort fan-out to other edges (never echoed back to this edge).
    virtual void publish_soft(const Uuid& id, std::vector<u8> payload) = 0;

    // Liveness lease of this edge; other edges get BackboneEdgeDown when it lapses.
    virtual void heartbeat_lease(u64 edge_id) = 0;

    [[nodiscard]] virtual bool clustered() const noexcept = 0;
    // True once the initial replay of every entry has been delivered.
    [[nodiscard]] virtual bool caught_up() const noexcept { return true; }
};

}  // namespace sb::backbone
