#include "op_table.hpp"

#include <utility>

namespace rb::client {

bool OpTable::track(u64 op_id, u32 method_id) {
    std::lock_guard lock(mutex_);
    return ops_.try_emplace(op_id, Entry{method_id, std::nullopt}).second;
}

bool OpTable::complete(u64 op_id, std::vector<u8> outcome) {
    std::lock_guard lock(mutex_);
    const auto it = ops_.find(op_id);
    if (it == ops_.end() || it->second.outcome) return false;
    it->second.outcome = std::move(outcome);
    return true;
}

OpState OpTable::state(u64 op_id) const {
    std::lock_guard lock(mutex_);
    const auto it = ops_.find(op_id);
    if (it == ops_.end()) return OpUnknown{};
    if (!it->second.outcome) return OpPending{};
    return *it->second.outcome;
}

bool OpTable::release(u64 op_id) {
    std::lock_guard lock(mutex_);
    return ops_.erase(op_id) != 0;
}

std::vector<PendingOp> OpTable::pending() const {
    std::lock_guard lock(mutex_);
    std::vector<PendingOp> out;
    for (const auto& [op_id, entry] : ops_)
        if (!entry.outcome) out.push_back(PendingOp{op_id, entry.method_id});
    return out;
}

}  // namespace rb::client
