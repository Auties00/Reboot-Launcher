#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "reboot/foundation/types.hpp"

namespace reboot::client {

struct OpPending {};
struct OpUnknown {};
using OpState = std::variant<OpUnknown, OpPending, std::vector<u8>>;

struct PendingOp {
    u64 op_id = 0;
    u32 method_id = 0;
};

// Covers no capability ids. Attached ops; each Outcome is stored once. Thread-safe.
class OpTable {
public:
    // `method_id` is 0 for an op attached by id; tracking a tracked op keeps its entry and is false.
    bool track(u64 op_id, u32 method_id);
    // False for an untracked or terminal op, so a replayed OpResult is ignored.
    [[nodiscard]] bool complete(u64 op_id, std::vector<u8> outcome);
    [[nodiscard]] OpState state(u64 op_id) const;
    // False when the op was not tracked.
    bool release(u64 op_id);

    // Re-attached after a same-epoch reconnect, or failed once the engine that ran them is gone.
    [[nodiscard]] std::vector<PendingOp> pending() const;

private:
    struct Entry {
        u32 method_id = 0;
        std::optional<std::vector<u8>> outcome;
    };

    mutable std::mutex mutex_;
    std::unordered_map<u64, Entry> ops_;
};

}  // namespace reboot::client
