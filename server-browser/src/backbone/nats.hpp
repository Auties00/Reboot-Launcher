#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#include <nats/nats.h>

#include "backbone/backbone.hpp"
#include "edge/config.hpp"

namespace sb::backbone {

// NATS backbone (server >= 2.11):
//   lifecycle: JetStream stream <stream> on "<prefix>.reg.<uuid-hex>", MaxMsgsPerSubject = 1,
//              compare-and-set via Nats-Expected-Last-Subject-Sequence, tombstones are empty
//              messages with a per-message TTL; an ordered consumer with DeliverLastPerSubject
//              replays the latest record of every entry on startup and then follows the tail.
//   soft:      core NATS "<prefix>.live.<uuid-hex>" on a dedicated no-echo connection that
//              flushes every publish immediately.
//   leases:    KV bucket with a TTL; a vanished key is a dead edge, and rendezvous hashing over
//              the live edges elects the one that tombstones its orphans.
class NatsBackbone final : public Backbone {
public:
    NatsBackbone(const edge::BackboneConfig& cfg, u64 edge_id);
    ~NatsBackbone() override;

    void start(Sink sink) override;
    void stop() override;
    void publish_record(const Uuid& id, std::vector<u8> payload, u64 expected_seq, u64 op) override;
    void publish_soft(const Uuid& id, std::vector<u8> payload) override;
    void heartbeat_lease(u64 edge_id) override;
    [[nodiscard]] bool clustered() const noexcept override { return true; }
    [[nodiscard]] bool caught_up() const noexcept override { return caught_up_.load(std::memory_order_acquire); }

private:
    static void on_record(natsConnection*, natsSubscription*, natsMsg* msg, void* closure);
    static void on_live(natsConnection*, natsSubscription*, natsMsg* msg, void* closure);
    static void on_ack(jsCtx*, natsMsg* msg, jsPubAck* pa, jsPubAckErr* pae, void* closure);
    void watch_leases(std::stop_token stop);
    natsOptions* make_options(bool live);
    void check(natsStatus s, const char* what);

    edge::BackboneConfig cfg_;
    u64 edge_id_;
    Sink sink_;
    natsConnection* js_conn_ = nullptr;
    natsConnection* live_conn_ = nullptr;
    jsCtx* js_ = nullptr;
    kvStore* leases_ = nullptr;
    natsSubscription* records_sub_ = nullptr;
    natsSubscription* live_sub_ = nullptr;
    std::string reg_prefix_;
    std::string live_prefix_;
    std::atomic<bool> caught_up_{false};
    std::jthread lease_watcher_;
    std::set<u64> live_edges_;
};

}  // namespace sb::backbone
