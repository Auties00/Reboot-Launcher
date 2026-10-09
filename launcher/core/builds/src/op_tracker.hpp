#pragma once

#include <map>
#include <utility>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"

namespace rb::builds {

// Strand-only. The ops a service still owes a completion, so its destructor can settle them, and
// the token its posted continuations check before touching the service.
class OpTracker {
public:
    explicit OpTracker(OpRegistry& ops) : ops_(ops) {}
    OpTracker(const OpTracker&) = delete;
    OpTracker& operator=(const OpTracker&) = delete;

    template <class T>
    void track(Operation<T>& op) {
        releases_.insert_or_assign(op.id(), [&op] { (void)op.complete(Cancelled{CancelReason::Shutdown}); });
    }

    // False when the op already had an outcome (cancel, deadline).
    template <class T>
    bool finish(Operation<T>& op, Outcome<T> outcome) {
        releases_.erase(op.id());
        return op.complete(std::move(outcome));
    }

    // Runs `work` on a worker; `done` runs on `strand` unless the service is gone by then.
    template <class T>
    void submit(WorkerPool& workers, Executor& strand, UniqueFunction<Result<T>(CancelToken)> work, CancelToken token,
                UniqueFunction<void(Result<T>)> done) {
        workers.submit<T>(std::move(work), std::move(token), strand,
                          [alive = alive_.token(), done = std::move(done)](Result<T> result) mutable {
                              if (!alive.cancelled()) done(std::move(result));
                          });
    }

    [[nodiscard]] CancelToken alive() const { return alive_.token(); }

    // From the owner's destructor: no continuation runs after this.
    void shutdown() {
        alive_.cancel(CancelReason::Shutdown);
        auto releases = std::move(releases_);
        for (auto& [id, release] : releases) {
            (void)ops_.cancel(id, CancelReason::Shutdown);
            release();
        }
    }

private:
    OpRegistry& ops_;
    std::map<OpId, UniqueFunction<void()>> releases_;
    CancelSource alive_;
};

// An op's outcome for a failed step: a cancelled step keeps the outcome the cancel decided.
template <class T>
[[nodiscard]] Outcome<T> failure(Diagnostic error) {
    if (error.kind == ErrorKind::Cancelled) return Cancelled{};
    return Failed{.error = std::move(error)};
}

}  // namespace rb::builds
