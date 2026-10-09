#pragma once

#include <mutex>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::client {

// What answers a Call, Start or SecretReveal: a Reply, or Started for an accepted Start.
using Answer = std::variant<contracts::ipc::Reply, contracts::ipc::Started>;
using AnswerDone = UniqueFunction<void(Result<Answer>)>;

// Covers no capability ids. Each call's `done` runs once, with no lock held. Thread-safe.
class PendingCalls {
public:
    // With kMaxOutstandingCalls already open, `done` runs at once with ipc.too_many_calls.
    [[nodiscard]] std::optional<u64> open(AnswerDone done);
    // An answer for a closed or unknown req_id is dropped.
    void answer(u64 req_id, Answer answer);
    // A deadline, a failed write or a lost link; a closed or unknown req_id is ignored.
    void fail(u64 req_id, const Diagnostic& reason);
    void fail_all(const Diagnostic& reason);

    [[nodiscard]] std::size_t open_count() const;

private:
    // Removes the call under the lock, so only one path ever runs its `done`.
    [[nodiscard]] AnswerDone take(u64 req_id);

    mutable std::mutex mutex_;
    std::unordered_map<u64, AnswerDone> calls_;
    u64 next_req_id_ = 1;
};

}  // namespace rb::client
