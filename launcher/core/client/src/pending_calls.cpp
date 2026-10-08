#include "pending_calls.hpp"

#include <utility>

#include "reboot/contracts/ipc.hpp"
#include "reboot/ipc/ipc_errors.hpp"

namespace reboot::client {

std::optional<u64> PendingCalls::open(AnswerDone done) {
    {
        std::lock_guard lock(mutex_);
        if (calls_.size() < contracts::ipc::kMaxOutstandingCalls) {
            const u64 req_id = next_req_id_++;
            calls_.emplace(req_id, std::move(done));
            return req_id;
        }
    }
    done(make_diag(ErrorDomain::Ipc, ipc::kTooManyCalls)
             .arg("limit", contracts::ipc::kMaxOutstandingCalls)
             .kind(ErrorKind::Conflict)
             .fail());
    return std::nullopt;
}

void PendingCalls::answer(u64 req_id, Answer answer) {
    if (AnswerDone done = take(req_id)) done(std::move(answer));
}

void PendingCalls::fail(u64 req_id, const Diagnostic& reason) {
    if (AnswerDone done = take(req_id)) done(std::unexpected(reason));
}

void PendingCalls::fail_all(const Diagnostic& reason) {
    std::unordered_map<u64, AnswerDone> failed;
    {
        std::lock_guard lock(mutex_);
        failed.swap(calls_);
    }
    for (auto& [req_id, done] : failed) done(std::unexpected(reason));
}

std::size_t PendingCalls::open_count() const {
    std::lock_guard lock(mutex_);
    return calls_.size();
}

AnswerDone PendingCalls::take(u64 req_id) {
    std::lock_guard lock(mutex_);
    const auto it = calls_.find(req_id);
    if (it == calls_.end()) return nullptr;
    AnswerDone done = std::move(it->second);
    calls_.erase(it);
    return done;
}

}  // namespace reboot::client
