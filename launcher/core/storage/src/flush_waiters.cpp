#include "flush_waiters.hpp"

#include <algorithm>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/executor.hpp"

namespace reboot::storage {

Diagnostic cancelled(std::string_view document) {
    return make_diag(ErrorDomain::Storage, msg::kCancelled)
        .kind(ErrorKind::Cancelled)
        .arg("document", document)
        .build();
}

FlushWaiters::~FlushWaiters() {
    for (Waiter& waiter : waiters_) waiter.registration.reset();
    alive_.cancel(CancelReason::Shutdown);
}

void FlushWaiters::add(const CancelToken& cancel, UniqueFunction<void(Result<void>)> done) {
    const u64 id = next_id_++;
    CancelRegistration registration =
        cancel.on_cancel([&strand = strand_, this, id, alive = alive_.token()](CancelReason) {
            strand.post([this, id, alive] {
                if (!alive.cancelled()) this->cancel(id);
            });
        });
    waiters_.push_back(Waiter{id, std::move(done), std::move(registration)});
}

void FlushWaiters::finish(const Result<void>& result) {
    std::vector<Waiter> finished = std::exchange(waiters_, {});
    for (Waiter& waiter : finished) {
        waiter.registration.reset();
        waiter.done(result);
    }
}

void FlushWaiters::cancel(u64 id) {
    const auto found = std::ranges::find(waiters_, id, &Waiter::id);
    if (found == waiters_.end()) return;
    UniqueFunction<void(Result<void>)> done = std::move(found->done);
    waiters_.erase(found);
    done(std::unexpected(cancelled(document_)));
}

}  // namespace reboot::storage
