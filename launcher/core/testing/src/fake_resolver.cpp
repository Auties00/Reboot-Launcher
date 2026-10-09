#include "reboot/testing/fake_resolver.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "messages.hpp"

namespace reboot::testing {
namespace {

constexpr u32 kLoopbackV4 = 0x7F000001;

struct Hang {};
using Answer = std::variant<std::vector<IpAddress>, Diagnostic, Hang>;

struct Lookup;

struct ResolverState {
    explicit ResolverState(Executor& executor) : deliver_on(executor) {}

    mutable std::mutex mutex;
    Executor& deliver_on;
    std::map<std::string, Answer> answers;
    std::vector<std::string> queries;
    std::size_t pending = 0;
    // Hung lookups, kept until their token fires or the resolver goes.
    std::vector<std::shared_ptr<Lookup>> hung;
};

// One resolve(): the answer and a cancel race, and only the first one calls `done`.
struct Lookup {
    std::shared_ptr<ResolverState> state;
    UniqueFunction<void(Result<std::vector<IpAddress>>)> done;
    CancelRegistration cancel;
    bool finished = false;
};

void finish(const std::shared_ptr<Lookup>& lookup, Result<std::vector<IpAddress>> result) {
    if (lookup->finished) return;
    lookup->finished = true;
    {
        const std::scoped_lock lock(lookup->state->mutex);
        --lookup->state->pending;
    }
    lookup->cancel.reset();
    std::shared_ptr<Lookup> keep;
    {
        const std::scoped_lock lock(lookup->state->mutex);
        auto& hung = lookup->state->hung;
        if (const auto it = std::ranges::find(hung, lookup); it != hung.end()) {
            keep = std::move(*it);
            hung.erase(it);
        }
    }
    auto done = std::move(lookup->done);
    if (done) done(std::move(result));
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

[[nodiscard]] Diagnostic unresolved(const std::string& host) {
    return make_diag(kTestingDomain, msg::kUnresolvedHost).arg("host", host).kind(ErrorKind::NotFound);
}

}  // namespace

struct FakeResolver::State : ResolverState {
    using ResolverState::ResolverState;
};

FakeResolver::FakeResolver(Executor& deliver_on) : state_(std::make_shared<State>(deliver_on)) {}

FakeResolver::~FakeResolver() {
    std::vector<std::shared_ptr<Lookup>> hung;
    const std::scoped_lock lock(state_->mutex);
    hung = std::move(state_->hung);
}

void FakeResolver::set(std::string host, std::vector<IpAddress> addresses) {
    const std::scoped_lock lock(state_->mutex);
    state_->answers.insert_or_assign(lower(host), std::move(addresses));
}

void FakeResolver::fail(std::string host, Diagnostic error) {
    const std::scoped_lock lock(state_->mutex);
    state_->answers.insert_or_assign(lower(host), std::move(error));
}

void FakeResolver::hang(std::string host) {
    const std::scoped_lock lock(state_->mutex);
    state_->answers.insert_or_assign(lower(host), Hang{});
}

std::vector<std::string> FakeResolver::queries() const {
    const std::scoped_lock lock(state_->mutex);
    return state_->queries;
}

std::size_t FakeResolver::pending() const {
    const std::scoped_lock lock(state_->mutex);
    return state_->pending;
}

void FakeResolver::resolve(std::string host, CancelToken token, UniqueFunction<void(Result<std::vector<IpAddress>>)> done) {
    auto lookup = std::make_shared<Lookup>();
    lookup->state = state_;
    lookup->done = std::move(done);

    std::optional<Answer> answer;
    const std::string key = lower(host);
    {
        const std::scoped_lock lock(state_->mutex);
        state_->queries.push_back(host);
        ++state_->pending;
        if (const auto it = state_->answers.find(key); it != state_->answers.end()) answer = it->second;
    }
    if (!answer) {
        if (const auto literal = IpAddress::parse(host)) {
            answer = std::vector<IpAddress>{*literal};
        } else if (key == "localhost" || key.ends_with(".localhost")) {
            IpAddress v6;
            v6.bytes[15] = 1;
            answer = std::vector<IpAddress>{IpAddress::v4(kLoopbackV4), v6};
        } else {
            answer = unresolved(host);
        }
    }

    lookup->cancel = token.on_cancel([weak = std::weak_ptr<Lookup>(lookup)](CancelReason) {
        if (const auto live = weak.lock())
            live->state->deliver_on.post([live] {
                finish(live, make_diag(kTestingDomain, msg::kCancelled).kind(ErrorKind::Cancelled).fail());
            });
    });
    if (std::holds_alternative<Hang>(*answer)) {
        const std::scoped_lock lock(state_->mutex);
        if (!lookup->finished) state_->hung.push_back(std::move(lookup));
        return;
    }
    state_->deliver_on.post([lookup, answer = std::move(*answer)]() mutable {
        if (auto* addresses = std::get_if<std::vector<IpAddress>>(&answer)) {
            finish(lookup, std::move(*addresses));
        } else {
            finish(lookup, std::unexpected(std::move(std::get<Diagnostic>(answer))));
        }
    });
}

}  // namespace reboot::testing
