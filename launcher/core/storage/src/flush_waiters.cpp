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

u64 FlushWaiters::add(const CancelToken& cancel, UniqueFunction<void(Result<void>)> done) {
    const u64 id = next_id_++;
    CancelRegistration registration =
        cancel.on_cancel([&strand = strand_, this, id, alive = alive_.token()](CancelReason) {
            strand.post([this, id, alive] {
                if (!alive.cancelled()) finish_one(id, std::unexpected(cancelled(document_)));
            });
        });
    waiters_.push_back(Waiter{id, std::move(done), std::move(registration)});
    return id;
}

void FlushWaiters::finish(const Result<void>& result) {
    std::vector<Waiter> finished = std::exchange(waiters_, {});
    for (Waiter& waiter : finished) {
        waiter.registration.reset();
        waiter.done(result);
    }
}

void FlushWaiters::finish_one(u64 id, Result<void> result) {
    const auto found = std::ranges::find(waiters_, id, &Waiter::id);
    if (found == waiters_.end()) return;
    Waiter waiter = std::move(*found);
    waiters_.erase(found);
    waiter.registration.reset();
    waiter.done(std::move(result));
}

}  // namespace reboot::storage
