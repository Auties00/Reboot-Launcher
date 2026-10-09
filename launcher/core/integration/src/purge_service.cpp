#include "reboot/integration/purge_service.hpp"

#include <utility>
#include <vector>

#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/integration/purge_error.hpp"
#include "reboot/ports/file_system.hpp"

namespace rb::integration {

namespace {

[[nodiscard]] Diagnostic blocked(PurgeScope scope, const PurgeBlockers& blockers) {
    return to_diagnostic(PurgeError{.code = PurgeErrorCode::Blocked,
                                    .scope = scope,
                                    .sessions = blockers.sessions.size(),
                                    .ops = blockers.ops.size(),
                                    .backend_running = blockers.backend_running});
}

// A root, or a folder holding the data root or the install.
[[nodiscard]] bool unsafe(const NativePath& target, const NativePath& data_root, const NativePath& install_dir) {
    const NativePath normal = target.lexically_normal();
    if (!normal.is_absolute() || normal.relative_path().empty()) return true;
    if (is_inside(data_root, normal)) return true;
    return !install_dir.empty() && is_inside(install_dir, normal);
}

[[nodiscard]] Progress deleting(u64 done, u64 total) {
    return Progress{.phase = "deleting", .done = done, .total = total};
}

// Reports each target as an item done, which also keeps the op's liveness deadline from expiring.
[[nodiscard]] Result<PurgeReport> delete_targets(ports::IFileSystem& fs, PurgeScope scope,
                                                 const std::vector<NativePath>& targets, const CancelToken& cancel,
                                                 Executor& strand, Operation<PurgeReport>& op) {
    PurgeReport report{.scope = scope, .removed = {}, .absent = {}};
    PurgeError failed{.code = PurgeErrorCode::RemoveFailed, .scope = scope};
    const u64 total = targets.size();
    u64 done = 0;
    for (const NativePath& target : targets) {
        // Posted before the reply that completes `op`, so `op` is still alive when these run.
        if (done > 0) strand.post([&op, done, total] { op.progress(deleting(done, total)); });
        ++done;
        if (cancel.cancelled()) break;
        if (Result<ports::FileRevision> found = fs.revision(target);
            !found && found.error().kind == ErrorKind::NotFound) {
            report.absent.push_back(target);
            continue;
        }
        if (Result<void> removed = fs.remove_tree(target); !removed) {
            if (!failed.path) failed.path = target;
            failed.causes.push_back(to_diagnostic(PurgeError{.code = PurgeErrorCode::RemoveFailed,
                                                             .scope = scope,
                                                             .path = target,
                                                             .causes = {std::move(removed.error())}}));
            continue;
        }
        report.removed.push_back(target);
    }
    if (!failed.causes.empty()) return std::unexpected(to_diagnostic(failed));
    return report;
}

}  // namespace

PurgeService::PurgeService(ports::IFileSystem& fs, const AppLayout& layout, const InstallLayout& install,
                           PurgeTargets targets, PurgeHooks hooks, WorkerPool& workers, Executor& strand,
                           OpRegistry& ops)
    : fs_(fs),
      layout_(layout),
      install_(install),
      targets_(std::move(targets)),
      hooks_(std::move(hooks)),
      workers_(workers),
      strand_(strand),
      ops_(ops) {}

Result<OpHandle> PurgeService::start_purge(PurgeScope scope, DisconnectPolicy policy) {
    PurgeBlockers blockers = hooks_.find_blockers ? hooks_.find_blockers(scope) : PurgeBlockers{};
    if (running_) blockers.ops.push_back(*running_);
    if (!blockers.empty()) return std::unexpected(blocked(scope, blockers));
    for (const NativePath& target : directories_for(targets_, scope))
        if (unsafe(target, layout_.root(), install_.install_dir))
            return std::unexpected(
                to_diagnostic(PurgeError{.code = PurgeErrorCode::UnsafeTarget, .scope = scope, .path = target}));

    auto [handle, op] = ops_.create<PurgeReport>(OpKind::Generic, policy, std::nullopt);
    running_ = handle.id();
    op.progress(Progress{.phase = "preparing"});
    if (!hooks_.prepare) {
        prepared(scope, op);
        return handle;
    }
    hooks_.prepare(scope, [this, scope, &op] { prepared(scope, op); });
    return handle;
}

void PurgeService::prepared(PurgeScope scope, Operation<PurgeReport>& op) {
    if (op.done()) return finish(scope, op, Cancelled{});
    // Something may have started while owners closed their files.
    if (hooks_.find_blockers) {
        if (const PurgeBlockers again = hooks_.find_blockers(scope); !again.empty())
            return finish(scope, op, Failed{blocked(scope, again)});
    }
    std::vector<NativePath> targets = directories_for(targets_, scope);
    op.progress(deleting(0, targets.size()));
    workers_.submit<PurgeReport>(
        [&fs = fs_, scope, targets = std::move(targets), &strand = strand_,
         &op](CancelToken cancel) -> Result<PurgeReport> {
            return delete_targets(fs, scope, targets, cancel, strand, op);
        },
        op.token(), strand_,
        [this, scope, &op](Result<PurgeReport> report) {
            if (!report) return finish(scope, op, Failed{std::move(report.error())});
            finish(scope, op, Completed<PurgeReport>{std::move(*report)});
        });
}

void PurgeService::finish(PurgeScope scope, Operation<PurgeReport>& op, Outcome<PurgeReport> outcome) {
    running_.reset();
    if (hooks_.on_purged) hooks_.on_purged(scope);
    op.complete(std::move(outcome));
}

}  // namespace rb::integration
