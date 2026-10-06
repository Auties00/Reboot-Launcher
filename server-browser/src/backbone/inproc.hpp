#pragma once

#include <mutex>

#include "backbone/backbone.hpp"
#include "core/flat_map.hpp"
#include "core/hash.hpp"

namespace sb::backbone {

// Single-process backbone for development and tests: same semantics as JetStream for
// compare-and-set and ordering, but no other edges (soft updates and leases are no-ops).
class InprocBackbone final : public Backbone {
public:
    void start(Sink sink) override;
    void stop() override {}
    void publish_record(const Uuid& id, std::vector<u8> payload, u64 expected_seq, u64 op) override;
    void publish_soft(const Uuid&, std::vector<u8>) override {}
    void heartbeat_lease(u64) override {}
    [[nodiscard]] bool clustered() const noexcept override { return false; }

private:
    std::mutex mu_;
    Sink sink_;
    FlatMap<Uuid, u64, UuidHash> last_seq_;
    u64 seq_ = 0;
};

}  // namespace sb::backbone
