#include "reboot/integration/integration_service.hpp"

#include <algorithm>
#include <utility>

#include "entry_ops.hpp"
#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/log.hpp"
#include "reboot/integration/entry_error.hpp"
#include "reboot/integration/integration_changed.hpp"
#include "reboot/integration/integration_policy.hpp"

namespace rb::integration {

namespace {

// What an apply or remove pass hands back to the strand.
struct ChangePass {
    std::vector<EntryStatus> requested;
    // The requested kinds it reached before a cancel.
    std::vector<IntegrationKind> processed;
    std::vector<EntryStatus> all;
};

[[nodiscard]] bool contains(const std::vector<IntegrationKind>& kinds, IntegrationKind kind) {
    return std::ranges::find(kinds, kind) != kinds.end();
}

[[nodiscard]] std::vector<IntegrationKind> unique_kinds(const std::vector<IntegrationKind>& kinds) {
    std::vector<IntegrationKind> out;
    for (const IntegrationKind kind : kinds)
        if (!contains(out, kind)) out.push_back(kind);
    return out;
}

// Every kind, taking the ones already read from `known`.
[[nodiscard]] std::vector<EntryStatus> inspect_all(ports::IIntegrationRegistrar& registrar,
                                                   const IntegrationTargets& targets,
                                                   const std::vector<EntryStatus>& known = {}) {
    std::vector<EntryStatus> all;
    all.reserve(kAllIntegrationKinds.size());
    for (const IntegrationKind kind : kAllIntegrationKinds) {
        const auto read = std::ranges::find(known, kind, &EntryStatus::kind);
        all.push_back(read != known.end() ? *read : inspect_entry(registrar, targets, kind));
    }
    return all;
}

void log_state_failure(const Diagnostic& error) {
    REBOOT_LOG_WARN(Storage, "integration state was not saved: {}", error.id);
}

}  // namespace

IntegrationService::IntegrationService(ports::IIntegrationRegistrar& registrar,
                                       storage::DocumentStore<storage::StateDocument>& state, WorkerPool& workers,
                                       Executor& strand, OpRegistry& ops, EventBus& events,
                                       IntegrationTargets targets)
    : registrar_(registrar),
      state_(state),
      workers_(workers),
      strand_(strand),
      ops_(ops),
      events_(events),
      targets_(std::move(targets)) {}

void IntegrationService::status(CancelToken token, UniqueFunction<void(std::vector<EntryStatus>)> done) {
    workers_.submit<std::vector<EntryStatus>>(
        [&registrar = registrar_, targets = targets_](CancelToken cancel) -> Result<std::vector<EntryStatus>> {
            if (cancel.cancelled()) return std::vector<EntryStatus>{};
            return inspect_all(registrar, targets);
        },
        std::move(token), strand_,
        [this, done = std::move(done)](Result<std::vector<EntryStatus>> read) mutable {
            std::vector<EntryStatus> entries = read ? std::move(*read) : std::vector<EntryStatus>{};
            fill_declined(entries);
            done(std::move(entries));
        });
}

Result<OpHandle> IntegrationService::start_apply(std::vector<IntegrationKind> kinds, DisconnectPolicy policy) {
    return start_change(JobKind::Apply, std::move(kinds), policy);
}

Result<OpHandle> IntegrationService::start_remove(std::vector<IntegrationKind> kinds, DisconnectPolicy policy) {
    return start_change(JobKind::Remove, std::move(kinds), policy);
}

void IntegrationService::reconcile(SemVer running, CancelToken token,
                                   UniqueFunction<void(std::optional<ReconcileReport>)> done) {
    enqueue(Job{.kind = JobKind::Reconcile,
                .kinds = {},
                .op = nullptr,
                .running = std::move(running),
                .token = std::move(token),
                .reconciled = std::move(done)});
}

Result<OpHandle> IntegrationService::start_change(JobKind kind, std::vector<IntegrationKind> kinds,
                                                  DisconnectPolicy policy) {
    if (kinds.empty()) return std::unexpected(to_diagnostic(EntryError{.code = EntryErrorCode::NoItems}));
    auto [handle, op] = ops_.create<std::vector<EntryStatus>>(OpKind::Generic, policy, std::nullopt);
    enqueue(Job{.kind = kind,
                .kinds = unique_kinds(kinds),
                .op = &op,
                .running = {},
                .token = op.token(),
                .reconciled = {}});
    return handle;
}

void IntegrationService::enqueue(Job job) {
    queue_.push_back(std::move(job));
    if (busy_) return;
    busy_ = true;
    strand_.post([this] { run_next(); });
}

void IntegrationService::finish_job() {
    strand_.post([this] { run_next(); });
}

void IntegrationService::run_next() {
    if (queue_.empty()) {
        busy_ = false;
        return;
    }
    Job job = std::move(queue_.front());
    queue_.pop_front();
    if (job.kind == JobKind::Reconcile) run_reconcile(std::move(job));
    else run_change(std::move(job));
}

void IntegrationService::run_change(Job job) {
    Operation<std::vector<EntryStatus>>& op = *job.op;
    // Cancelled or timed out while queued: complete so the registry can free it.
    if (op.done()) {
        op.complete(Cancelled{});
        return finish_job();
    }
    const bool apply = job.kind == JobKind::Apply;
    op.progress(Progress{.phase = apply ? "applying" : "removing"});
    workers_.submit<ChangePass>(
        [&registrar = registrar_, targets = targets_, kinds = std::move(job.kinds),
         apply](CancelToken cancel) -> Result<ChangePass> {
            ChangePass pass;
            for (const IntegrationKind kind : kinds) {
                if (cancel.cancelled()) break;
                const EntryStatus found = inspect_entry(registrar, targets, kind);
                pass.requested.push_back(apply ? apply_entry(registrar, targets, found)
                                               : remove_entry(registrar, targets, found));
                pass.processed.push_back(kind);
            }
            pass.all = inspect_all(registrar, targets, pass.requested);
            return pass;
        },
        op.token(), strand_,
        [this, &op, apply](Result<ChangePass> result) {
            if (!result) {
                op.complete(Failed{std::move(result.error())});
                return finish_job();
            }
            const std::vector<IntegrationKind>& processed = result->processed;
            if (!processed.empty()) {
                Result<u64> saved = state_.update([&](storage::StateDocument& document) {
                    std::erase_if(document.declined_integrations,
                                  [&](IntegrationKind kind) { return contains(processed, kind); });
                    if (!apply)
                        for (const IntegrationKind kind : processed) document.declined_integrations.push_back(kind);
                });
                if (!saved) log_state_failure(saved.error());
            }
            fill_declined(result->requested);
            fill_declined(result->all);
            events_.publish(EventKind::IntegrationChanged, IntegrationChanged{std::move(result->all)});
            op.complete(Completed<std::vector<EntryStatus>>{std::move(result->requested)});
            finish_job();
        });
}

void IntegrationService::run_reconcile(Job job) {
    std::optional<SemVer> previous = state_.get().last_run_version;
    if (job.token.cancelled() || previous == job.running) {
        job.reconciled(std::nullopt);
        return finish_job();
    }
    CancelToken token = job.token;
    workers_.submit<ReconcileReport>(
        [&registrar = registrar_, targets = targets_,
         declined = state_.get().declined_integrations](CancelToken cancel) -> Result<ReconcileReport> {
            ReconcileReport report;
            for (const IntegrationKind kind : kAllIntegrationKinds) {
                EntryStatus found = inspect_entry(registrar, targets, kind);
                if (cancel.cancelled() ||
                    reconcile_action(kind, found.state, contains(declined, kind)) == ReconcileAction::Leave) {
                    report.items.push_back(std::move(found));
                    continue;
                }
                EntryStatus written = write_entry(registrar, targets, found);
                if (!written.detail) report.written.push_back(kind);
                report.items.push_back(std::move(written));
            }
            return report;
        },
        std::move(token), strand_,
        [this, job = std::move(job), previous = std::move(previous)](Result<ReconcileReport> result) mutable {
            if (!result) {
                REBOOT_LOG_WARN(Engine, "integration reconcile failed: {}", result.error().id);
                job.reconciled(std::nullopt);
                return finish_job();
            }
            fill_declined(result->items);
            if (!result->written.empty())
                events_.publish(EventKind::IntegrationChanged, IntegrationChanged{result->items});
            if (job.token.cancelled()) {
                job.reconciled(std::nullopt);
                return finish_job();
            }
            Result<u64> saved =
                state_.update([&](storage::StateDocument& document) { document.last_run_version = job.running; });
            if (!saved) log_state_failure(saved.error());
            result->previous = std::move(previous);
            result->running = std::move(job.running);
            job.reconciled(std::move(*result));
            finish_job();
        });
}

void IntegrationService::fill_declined(std::vector<EntryStatus>& entries) const {
    const std::vector<IntegrationKind>& declined = state_.get().declined_integrations;
    for (EntryStatus& entry : entries) entry.declined = contains(declined, entry.kind);
}

}  // namespace rb::integration
