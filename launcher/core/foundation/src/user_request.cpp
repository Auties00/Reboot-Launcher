#include "reboot/foundation/user_request.hpp"

#include <map>
#include <utility>

#include "messages.hpp"
#include "reboot/foundation/events.hpp"

namespace reboot {

struct UserRequestRegistry::Impl {
    struct Entry {
        UserRequest request;
        UniqueFunction<Result<void>(const std::any&)> on_answer;
        CancelRegistration withdrawal;
        // Set while on_answer runs; a withdrawal in that window waits for its verdict.
        bool answering = false;
        bool withdraw_after_answer = false;
    };

    explicit Impl(EventBus& bus) : events(bus) {}

    void resolve(RequestId id, RequestResolution resolution) {
        const auto it = entries.find(id);
        if (it == entries.end()) return;
        const EventScope scope{it->second.request.session, it->second.request.op, {}};
        // Destroyed after the erase, so a registration callback cannot re-enter a half-erased map.
        Entry removed = std::move(it->second);
        entries.erase(it);
        events.publish(EventKind::UserActionResolved, UserActionResolvedEvent{id, resolution}, scope);
    }

    void withdraw(RequestId id) {
        const auto it = entries.find(id);
        if (it == entries.end()) return;
        if (it->second.answering) {
            it->second.withdraw_after_answer = true;
            return;
        }
        resolve(id, RequestResolution::Withdrawn);
    }

    EventBus& events;
    u64 next_id = 1;
    std::map<RequestId, Entry> entries;
};

UserRequestRegistry::UserRequestRegistry(EventBus& events) : impl_(std::make_unique<Impl>(events)) {}

UserRequestRegistry::~UserRequestRegistry() = default;

RequestId UserRequestRegistry::ask(UserRequestKind kind, std::any payload, std::optional<OpId> op,
                                   std::optional<SessionId> session,
                                   UniqueFunction<Result<void>(const std::any& answer)> on_answer, CancelToken token) {
    const RequestId id{impl_->next_id++};
    UserRequest request{id, kind, std::move(payload), op, session};
    impl_->events.publish(EventKind::UserActionRequired, request, EventScope{session, op, {}});
    impl_->entries.emplace(id, Impl::Entry{std::move(request), std::move(on_answer), {}});

    // Runs at once when the token is already cancelled, which withdraws the request just raised.
    CancelRegistration withdrawal = token.on_cancel([impl = impl_.get(), id](CancelReason) { impl->withdraw(id); });
    if (const auto it = impl_->entries.find(id); it != impl_->entries.end()) it->second.withdrawal = std::move(withdrawal);
    return id;
}

Result<void> UserRequestRegistry::respond(RequestId id, std::any answer) {
    const auto it = impl_->entries.find(id);
    if (it == impl_->entries.end() || it->second.answering) {
        const bool issued = id.value != 0 && id.value < impl_->next_id;
        return make_diag(ErrorDomain::Requests, issued ? msg::kRequestAlreadyResolved : msg::kRequestNotFound)
            .kind(issued ? ErrorKind::Conflict : ErrorKind::NotFound)
            .arg("request", id.value)
            .fail();
    }

    it->second.answering = true;
    Result<void> verdict = it->second.on_answer(answer);
    // on_answer may have raised or resolved other requests; only this one is guaranteed to remain.
    Impl::Entry& entry = impl_->entries.at(id);
    entry.answering = false;
    if (verdict) {
        impl_->resolve(id, RequestResolution::Answered);
    } else if (entry.withdraw_after_answer) {
        impl_->resolve(id, RequestResolution::Withdrawn);
    }
    return verdict;
}

std::vector<UserRequest> UserRequestRegistry::pending() const {
    std::vector<UserRequest> out;
    out.reserve(impl_->entries.size());
    for (const auto& entry : impl_->entries) out.push_back(entry.second.request);
    return out;
}

}  // namespace reboot
