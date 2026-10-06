#include "backbone/inproc.hpp"

#include "registry/events.hpp"

namespace sb::backbone {

using registry::BackboneAck;
using registry::BackboneRecord;
using registry::kNoShard;
using registry::ReplicaMsg;

void InprocBackbone::start(Sink sink) {
    std::lock_guard lk(mu_);
    sink_ = std::move(sink);
}

void InprocBackbone::publish_record(const Uuid& id, std::vector<u8> payload, u64 expected_seq, u64 op) {
    std::lock_guard lk(mu_);
    auto it = last_seq_.find(id);
    const u64 current = it == last_seq_.end() ? 0 : it->second;
    if (current != expected_seq) {
        sink_(new ReplicaMsg(kNoShard, 0, BackboneAck{.op = op, .ok = false, .stream_seq = current}));
        return;
    }
    const u64 seq = ++seq_;
    last_seq_[id] = seq;
    sink_(new ReplicaMsg(kNoShard, 0, BackboneRecord{.stream_seq = seq, .payload = std::move(payload), .id = id}));
    sink_(new ReplicaMsg(kNoShard, 0, BackboneAck{.op = op, .ok = true, .stream_seq = seq}));
}

}  // namespace sb::backbone
