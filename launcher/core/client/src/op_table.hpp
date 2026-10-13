#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace rb::client {

struct OpPending {};
struct OpUnknown {};
using OpState = std::variant<OpUnknown, OpPending, std::vector<u8>>;

struct PendingOp {
    u64 op_id = 0;
    u32 method_id = 0;
    // The epoch of the engine that runs it.
    u64 epoch = 0;
};

// Covers no capability ids. Attached ops; each Outcome is stored once. Thread-safe.
class OpTable {
public:
    // `method_id` is 0 for an op attached by id. Engines number their ops afresh, so an op of
    // another epoch with the same id is a different op, which this one replaces; tracking an op
    // already tracked in `epoch` keeps its entry and is false.
    bool track(u64 op_id, u32 method_id, u64 epoch);
    // False for an op not tracked in `epoch` or already terminal, so a replayed OpResult is ignored.
    [[nodiscard]] bool complete(u64 op_id, u64 epoch, std::vector<u8> outcome);
    [[nodiscard]] OpState state(u64 op_id) const;
    // False when the op was not tracked.
    bool release(u64 op_id);

    // Re-attached after a same-epoch reconnect, or failed once the engine that ran them is gone.
    [[nodiscard]] std::vector<PendingOp> pending() const;

private:
    struct Entry {
        u32 method_id = 0;
        u64 epoch = 0;
        std::optional<std::vector<u8>> outcome;
    };

    mutable std::mutex mutex_;
    std::unordered_map<u64, Entry> ops_;
};

}  // namespace rb::client
