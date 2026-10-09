#include "reboot/host/host_port_allocator.hpp"

#include <algorithm>
#include <deque>
#include <expected>
#include <utility>

#include "reboot/foundation/executor.hpp"
#include "reboot/host/host_error.hpp"
#include "reboot/net/port_conflict.hpp"
#include "reboot/net/port_preflight.hpp"

namespace reboot::host {

namespace {

[[nodiscard]] Diagnostic error_of(HostError error) { return to_diagnostic(error); }

[[nodiscard]] Diagnostic cancelled() { return error_of({.code = HostErrorCode::Cancelled}); }

[[nodiscard]] bool covers_reserved(const PortBlock& block) noexcept { return block.contains(kReservedBackendPort); }

// The last port a block of `size` may start at inside `range`, if any fits.
[[nodiscard]] std::optional<u32> last_start(const PortRange& range, u16 size) noexcept {
    const u32 span = u32{range.last.value} - range.first.value + 1;
    if (size > span) return std::nullopt;
    return u32{range.last.value} - size + 1;
}

}  // namespace

struct HostPortAllocator::Impl {
    // One reserve() or recheck() waiting for its turn; reservations run one at a time, so the
    // candidates of each see every block granted before it.
    struct Job {
        u64 id = 0;
        SessionId session;
        PortPolicy policy;
        u16 size = 0;
        std::optional<PortBlock> after;
        // Set for a recheck: exactly this block, which the session keeps.
        std::optional<PortBlock> recheck;
        std::vector<net::OurProcess> ours;
        CancelToken token;
        CancelRegistration on_cancel;
        UniqueFunction<void(Result<PortBlock>)> done;
    };

    Impl(net::PortPreflight& preflight_in, WorkerPool& workers_in, Executor& strand_in)
        : preflight(preflight_in), workers(workers_in), strand(strand_in) {}

    template <class F>
    void post(F&& task) {
        strand.post([alive_token = alive.token(), task = std::forward<F>(task)]() mutable {
            if (!alive_token.cancelled()) task();
        });
    }

    Result<void> enqueue(std::unique_ptr<Job> job) {
        job->id = ++last_job;
        Job& queued = *job;
        queue.push_back(std::move(job));
        // The callback may run on another thread; the strand settles it.
        queued.on_cancel = queued.token.on_cancel([this, alive_token = alive.token(), id = queued.id](CancelReason) {
            if (alive_token.cancelled()) return;
            post([this, id] { cancel_queued(id); });
        });
        post([this] { pump(); });
        return {};
    }

    void cancel_queued(u64 id) {
        const auto found = std::ranges::find(queue, id, [](const std::unique_ptr<Job>& queued) { return queued->id; });
        if (found == queue.end()) return;
        std::unique_ptr<Job> removed = std::move(*found);
        queue.erase(found);
        finish(*removed, std::unexpected(cancelled()));
    }

    static void finish(Job& job, Result<PortBlock> result) {
        job.on_cancel.reset();
        if (UniqueFunction<void(Result<PortBlock>)> done = std::move(job.done)) done(std::move(result));
    }

    [[nodiscard]] std::optional<PortBlock> held_by_other(const SessionId& session, const PortBlock& block) const {
        for (const auto& [owner, held] : blocks)
            if (owner != session && held.overlaps(block)) return held;
        return std::nullopt;
    }

    // Candidates in the order they are tried, or why there can be none.
    [[nodiscard]] Result<std::vector<PortBlock>> candidates(const Job& job) const {
        if (job.recheck) return std::vector<PortBlock>{*job.recheck};
        if (const auto* pinned = std::get_if<PinnedPorts>(&job.policy)) {
            const PortBlock block{pinned->first, job.size};
            if (const std::optional<PortBlock> other = held_by_other(job.session, block)) {
                const Port first_shared{std::max(block.first.value, other->first.value)};
                return std::unexpected(error_of({.code = HostErrorCode::BlockInUse, .port = first_shared}));
            }
            return std::vector<PortBlock>{block};
        }
        const PortRange& range = std::get<AutoPorts>(job.policy).range;
        std::vector<PortBlock> out;
        u32 start = range.first.value;
        if (job.after) start = std::max(start, u32{job.after->first.value} + job.after->size);
        if (const std::optional<u32> last = last_start(range, job.size))
            for (u32 first = start; first <= *last; ++first) {
                const PortBlock block{Port{static_cast<u16>(first)}, job.size};
                if (!covers_reserved(block) && !held_by_other(job.session, block)) out.push_back(block);
            }
        if (out.empty())
            return std::unexpected(error_of({.code = HostErrorCode::NoFreeBlock, .range = range, .block_size = job.size}));
        return out;
    }

    void pump() {
        if (running || queue.empty()) return;
        std::unique_ptr<Job> job = std::move(queue.front());
        queue.pop_front();
        if (job->token.cancelled()) {
            finish(*job, std::unexpected(cancelled()));
            return pump();
        }
        Result<std::vector<PortBlock>> tried = candidates(*job);
        if (!tried) {
            finish(*job, std::unexpected(std::move(tried.error())));
            return pump();
        }
        running = std::move(job);
        const bool any_block = std::holds_alternative<AutoPorts>(running->policy) && !running->recheck;
        const std::optional<PortRange> range =
            any_block ? std::optional(std::get<AutoPorts>(running->policy).range) : std::nullopt;
        workers.submit<PortBlock>(
            [&preflight = preflight, blocks = std::move(*tried), ours = running->ours, range,
             size = running->size](CancelToken token) -> Result<PortBlock> {
                std::optional<Diagnostic> first_conflict;
                for (const PortBlock& block : blocks) {
                    if (token.cancelled()) return std::unexpected(cancelled());
                    const std::vector<Port> ports = block.ports();
                    auto free = preflight.require_free(net::PortProtocol::Udp, IpAddress::v4(0), ports, ours);
                    if (!free) return std::unexpected(std::move(free.error()));
                    if (free->has_value()) return block;
                    Diagnostic conflict = net::to_diagnostic(free->error());
                    if (!range) return std::unexpected(std::move(conflict));
                    if (!first_conflict) first_conflict = std::move(conflict);
                }
                HostError none{.code = HostErrorCode::NoFreeBlock, .range = *range, .block_size = size};
                none.cause = std::move(first_conflict);
                return std::unexpected(error_of(std::move(none)));
            },
            running->token, strand,
            [this, alive_token = alive.token()](Result<PortBlock> result) {
                if (!alive_token.cancelled()) on_checked(std::move(result));
            });
    }

    void on_checked(Result<PortBlock> result) {
        std::unique_ptr<Job> job = std::move(running);
        if (job->token.cancelled()) {
            finish(*job, std::unexpected(cancelled()));
        } else {
            if (result) {
                const auto held = std::ranges::find(blocks, job->session, &std::pair<SessionId, PortBlock>::first);
                if (held != blocks.end()) held->second = *result;
                else blocks.emplace_back(job->session, *result);
            }
            finish(*job, std::move(result));
        }
        pump();
    }

    net::PortPreflight& preflight;
    WorkerPool& workers;
    Executor& strand;
    std::deque<std::unique_ptr<Job>> queue;
    std::unique_ptr<Job> running;
    u64 last_job = 0;
    std::vector<std::pair<SessionId, PortBlock>> blocks;
    CancelSource alive;
};

HostPortAllocator::HostPortAllocator(net::PortPreflight& preflight, WorkerPool& workers, Executor& strand)
    : impl_(std::make_unique<Impl>(preflight, workers, strand)) {}

HostPortAllocator::~HostPortAllocator() { impl_->alive.cancel(CancelReason::Shutdown); }

Result<void> HostPortAllocator::reserve(BlockRequest request, CancelToken token,
                                        UniqueFunction<void(Result<PortBlock>)> done) {
    const auto out_of_range = [&](Port port) {
        return std::unexpected(error_of({.code = HostErrorCode::BlockOutOfRange, .port = port, .block_size = request.size}));
    };
    if (Result<void> valid = validate(request.policy); !valid) return valid;
    if (const auto* pinned = std::get_if<PinnedPorts>(&request.policy)) {
        const PortBlock block{pinned->first, request.size};
        if (!block.fits()) return out_of_range(pinned->first);
        if (covers_reserved(block))
            return std::unexpected(error_of({.code = HostErrorCode::ReservedPort, .port = kReservedBackendPort}));
    } else {
        const PortRange& range = std::get<AutoPorts>(request.policy).range;
        if (request.size == 0 || !last_start(range, request.size)) return out_of_range(range.first);
    }

    auto job = std::make_unique<Impl::Job>();
    job->session = request.session;
    job->policy = request.policy;
    job->size = request.size;
    job->after = request.after;
    job->ours = std::move(request.ours);
    job->token = std::move(token);
    job->done = std::move(done);
    return impl_->enqueue(std::move(job));
}

Result<void> HostPortAllocator::recheck(SessionId session, std::vector<net::OurProcess> ours, CancelToken token,
                                        UniqueFunction<void(Result<PortBlock>)> done) {
    const std::optional<PortBlock> current = block(session);
    if (!current) return std::unexpected(internal_bug("host_port_allocator.recheck"));
    auto job = std::make_unique<Impl::Job>();
    job->session = session;
    job->policy = PinnedPorts{current->first};
    job->size = current->size;
    job->recheck = current;
    job->ours = std::move(ours);
    job->token = std::move(token);
    job->done = std::move(done);
    return impl_->enqueue(std::move(job));
}

void HostPortAllocator::release(SessionId session) {
    std::erase_if(impl_->blocks, [&](const auto& held) { return held.first == session; });
}

std::optional<PortBlock> HostPortAllocator::block(SessionId session) const {
    const auto held = std::ranges::find(impl_->blocks, session, &std::pair<SessionId, PortBlock>::first);
    if (held == impl_->blocks.end()) return std::nullopt;
    return held->second;
}

}  // namespace reboot::host
