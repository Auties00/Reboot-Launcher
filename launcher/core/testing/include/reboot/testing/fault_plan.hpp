#pragma once

#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::testing {

// Covers no capability ids (decision testing-strategy).
// Makes chosen calls of a fake fail. Thread-safe, because some ports are called from workers.
template <class Operation>
class FaultPlan {
public:
    // Fails the next `count` calls of `operation`; a count of 0 arms nothing.
    void fail_next(Operation operation, Diagnostic error, u32 count = 1) {
        if (count == 0) return;
        const std::scoped_lock lock(mutex_);
        armed_.push_back(Armed{operation, std::move(error), count});
    }

    void fail_always(Operation operation, Diagnostic error) {
        const std::scoped_lock lock(mutex_);
        armed_.push_back(Armed{operation, std::move(error), std::nullopt});
    }

    void clear() {
        const std::scoped_lock lock(mutex_);
        armed_.clear();
    }

    // The fake calls this first; the oldest armed fault for `operation` wins and loses one count.
    [[nodiscard]] std::optional<Diagnostic> take(Operation operation) {
        const std::scoped_lock lock(mutex_);
        for (auto it = armed_.begin(); it != armed_.end(); ++it) {
            if (it->operation != operation) continue;
            Diagnostic error = it->error;
            if (it->remaining && --*it->remaining == 0) armed_.erase(it);
            return error;
        }
        return std::nullopt;
    }

private:
    struct Armed {
        Operation operation;
        Diagnostic error;
        std::optional<u32> remaining;
    };

    std::mutex mutex_;
    std::vector<Armed> armed_;
};

}  // namespace reboot::testing
