#pragma once

#include <string>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class Executor;
}

namespace reboot::storage {

// Strand-only. Flush callbacks; a cancelled one gets storage.cancelled at once while writes go on.
class FlushWaiters {
public:
    FlushWaiters(Executor& strand, std::string document) : strand_(strand), document_(std::move(document)) {}
    ~FlushWaiters();
    FlushWaiters(const FlushWaiters&) = delete;
    FlushWaiters& operator=(const FlushWaiters&) = delete;

    void add(const CancelToken& cancel, UniqueFunction<void(Result<void>)> done);
    void finish(const Result<void>& result);

private:
    struct Waiter {
        u64 id = 0;
        UniqueFunction<void(Result<void>)> done;
        CancelRegistration registration;
    };

    void cancel(u64 id);

    Executor& strand_;
    std::string document_;
    std::vector<Waiter> waiters_;
    u64 next_id_ = 1;
    // Cancelled on destruction, so a cancellation posted earlier never touches this object.
    CancelSource alive_;
};

// A storage.cancelled diagnostic for `document`.
[[nodiscard]] Diagnostic cancelled(std::string_view document);

}  // namespace reboot::storage
