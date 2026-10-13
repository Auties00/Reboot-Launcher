#pragma once

#include <string>
#include <vector>

#include "reboot/base/diag.hpp"
#include "reboot/base/function.hpp"
#include "reboot/base/types.hpp"
#include "reboot/foundation/cancel.hpp"

namespace rb {
class Executor;
}

namespace rb::storage {

// Strand-only. Write completion callbacks; a cancelled one gets storage.cancelled at once while writes go on.
class FlushWaiters {
public:
    FlushWaiters(Executor& strand, std::string document) : strand_(strand), document_(std::move(document)) {}
    ~FlushWaiters();
    FlushWaiters(const FlushWaiters&) = delete;
    FlushWaiters& operator=(const FlushWaiters&) = delete;

    // Returns the id finish_one() takes.
    u64 add(const CancelToken& cancel, UniqueFunction<void(Result<void>)> done);
    void finish(const Result<void>& result);
    // Does nothing for a waiter already finished or cancelled.
    void finish_one(u64 id, Result<void> result);

private:
    struct Waiter {
        u64 id = 0;
        UniqueFunction<void(Result<void>)> done;
        CancelRegistration registration;
    };

    Executor& strand_;
    std::string document_;
    std::vector<Waiter> waiters_;
    u64 next_id_ = 1;
    // Cancelled on destruction, so a cancellation posted earlier never touches this object.
    CancelSource alive_;
};

// A storage.cancelled diagnostic for `document`.
[[nodiscard]] Diagnostic cancelled(std::string_view document);

}  // namespace rb::storage
