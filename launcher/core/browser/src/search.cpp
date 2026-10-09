#include "reboot/browser/search.hpp"

#include <algorithm>
#include <map>
#include <utility>

#include "messages.hpp"
#include "reboot/browser/browser_session.hpp"
#include "reboot/browser/deep_link.hpp"
#include "reboot/browser/own_servers.hpp"
#include "reboot/browser/token_bucket.hpp"
#include "reboot/foundation/executor.hpp"
#include "text_util.hpp"
#include "wire_mapping.hpp"

namespace reboot::browser {

namespace {

namespace wire = sb::wire;

constexpr std::size_t kMinTextBytes = 1;
constexpr std::size_t kMaxTextBytes = 64;
// RATE_LIMITED answers retried after their hint before the search gives up.
constexpr u32 kRateLimitRetries = 3;

[[nodiscard]] Diagnostic cancelled() { return to_diagnostic(RbsbRequestError{.failure = RbsbFailure::Cancelled}); }

}  // namespace

struct Search::Impl {
    struct Alive {
        Impl* impl = nullptr;
    };

    struct Run {
        u64 id = 0;
        ConnectionId client;
        SearchRequest request;
        std::optional<ServerId> by_id;
        // Cancelled by the caller's token or by a newer search from the same client.
        CancelSource cancel;
        CancelRegistration link;
        UniqueFunction<void(Result<SearchPage>)> done;
        TimerHandle timer;
        u32 rate_retries = 0;
        bool finished = false;
    };

    using RunPtr = std::shared_ptr<Run>;

    Impl(BrowserSession& session_in, const IOwnServers& own_in, TimerService& timers_in, const IClock& clock_in)
        : session(session_in), own(own_in), timers(timers_in), rate(clock_in, kQueryRate),
          alive(std::make_shared<Alive>(Alive{this})) {}

    ~Impl() { alive->impl = nullptr; }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void finish(const RunPtr& run, Result<SearchPage> result) {
        if (run->finished) return;
        run->finished = true;
        run->timer.cancel();
        run->link.reset();
        if (const auto it = runs.find(run->client); it != runs.end() && it->second == run) runs.erase(it);
        run->done(std::move(result));
    }

    void arm(const RunPtr& run, std::chrono::milliseconds delay) {
        run->timer = timers.after(delay, [weak = alive, run] {
            if (Impl* self = weak->impl) self->send(run);
        });
    }

    // Debounce and pacing waits end here; the caller's cancellation is noticed at the latest now.
    void send(const RunPtr& run) {
        if (run->finished) return;
        if (run->cancel.cancelled()) {
            finish(run, std::unexpected(cancelled()));
            return;
        }
        if (const auto wait = rate.try_take()) {
            arm(run, *wait);
            return;
        }
        if (run->by_id) {
            session.resolve(*run->by_id, run->cancel.token(),
                            [weak = alive, run](RbsbResult<wire::ResolveResult> result) mutable {
                                if (Impl* self = weak->impl) self->on_resolved(run, std::move(result));
                            });
            return;
        }
        const SearchRequest& request = run->request;
        wire::Query query;
        query.view = wire::ViewSpec{request.bucket, to_wire(request.password), to_wire(request.region), to_wire(request.sort)};
        query.text = request.text;
        query.limit = request.limit;
        if (session.edge() && session.edge()->limits.max_query_limit != 0)
            query.limit = std::min(query.limit, session.edge()->limits.max_query_limit);
        query.cursor = request.cursor.bytes;
        session.query(std::move(query), run->cancel.token(), [weak = alive, run](RbsbResult<wire::QueryResult> result) mutable {
            if (Impl* self = weak->impl) self->on_page(run, std::move(result));
        });
    }

    [[nodiscard]] std::chrono::milliseconds clock_offset() const {
        return session.edge() ? session.edge()->clock_offset : std::chrono::milliseconds{0};
    }

    // True when the failure was a RATE_LIMITED answer that will be retried.
    bool retry_rate_limited(const RunPtr& run, const RbsbRequestError& error) {
        if (error.failure != RbsbFailure::Rejected || error.code != wire::ErrorCode::rate_limited ||
            run->rate_retries >= kRateLimitRetries)
            return false;
        ++run->rate_retries;
        rate.hold_off(error.retry_after);
        arm(run, error.retry_after);
        return true;
    }

    void fail(const RunPtr& run, const RbsbRequestError& error) {
        finish(run, std::unexpected(error.failure == RbsbFailure::Cancelled ? cancelled() : to_diagnostic(error)));
    }

    void on_page(const RunPtr& run, RbsbResult<wire::QueryResult> result) {
        if (run->finished) return;
        if (!result) {
            if (!retry_rate_limited(run, result.error())) fail(run, result.error());
            return;
        }
        SearchPage page;
        const std::chrono::milliseconds offset = clock_offset();
        for (const wire::ListEntry& entry : result->entries)
            if (!own.owns(ServerId{entry.id})) page.rows.push_back(make_server_row(entry, offset));
        if (!result->next_cursor.empty()) page.next = SearchCursor{std::move(result->next_cursor)};
        page.total = result->total;
        finish(run, std::move(page));
    }

    void on_resolved(const RunPtr& run, RbsbResult<wire::ResolveResult> result) {
        if (run->finished) return;
        SearchPage page;
        page.resolved_by_id = true;
        if (!result) {
            const RbsbRequestError& error = result.error();
            if (retry_rate_limited(run, error)) return;
            // An id the edge does not know is an empty page, as a name that matches nothing is.
            if (error.failure != RbsbFailure::Rejected || error.code != wire::ErrorCode::not_found) {
                fail(run, error);
                return;
            }
            finish(run, std::move(page));
            return;
        }
        if (result->details) {
            page.total = 1;
            if (!own.owns(ServerId{result->details->entry.id}))
                page.rows.push_back(make_server_row(result->details->entry, clock_offset()));
        }
        finish(run, std::move(page));
    }

    BrowserSession& session;
    const IOwnServers& own;
    TimerService& timers;
    // Mirrors the edge's per-connection query limiter, shared by every client's searches.
    TokenBucket rate;
    std::shared_ptr<Alive> alive;
    u64 next_run = 1;
    std::map<ConnectionId, RunPtr> runs;
};

Search::Search(BrowserSession& session, const IOwnServers& own, TimerService& timers, const IClock& clock)
    : impl_(std::make_unique<Impl>(session, own, timers, clock)) {}

Search::~Search() = default;

Result<void> Search::run(ConnectionId client, SearchRequest request, CancelToken token,
                         UniqueFunction<void(Result<SearchPage>)> done) {
    const std::string_view text = trim_ascii(request.text);
    if (text.size() < kMinTextBytes || text.size() > kMaxTextBytes)
        return make_diag(ErrorDomain::Browser, kSearchTextLength)
            .arg("min", kMinTextBytes)
            .arg("max", kMaxTextBytes)
            .kind(ErrorKind::InvalidInput)
            .fail();

    auto run = std::make_shared<Impl::Run>();
    run->id = impl_->next_run++;
    run->client = client;
    if (const auto link = parse_deep_link(text)) run->by_id = link->server;
    request.text = std::string(text);
    run->request = std::move(request);
    run->done = std::move(done);

    if (const auto previous = impl_->runs.find(client); previous != impl_->runs.end()) {
        const Impl::RunPtr superseded = previous->second;
        impl_->runs.erase(previous);
        superseded->cancel.cancel(CancelReason::Superseded);
        // Still waiting in the debounce: nothing in flight will answer it.
        if (superseded->timer.active()) impl_->finish(superseded, std::unexpected(cancelled()));
    }
    impl_->runs[client] = run;
    // CancelSource::cancel is thread-safe, so the caller's token may fire on any thread.
    run->link = token.on_cancel([weak_run = std::weak_ptr<Impl::Run>(run)](CancelReason reason) {
        if (const Impl::RunPtr owner = weak_run.lock()) owner->cancel.cancel(reason);
    });
    impl_->arm(run, kSearchDebounce);
    return {};
}

}  // namespace reboot::browser
