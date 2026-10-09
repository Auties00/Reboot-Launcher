#pragma once

#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/integration/purge_hooks.hpp"
#include "reboot/integration/purge_report.hpp"
#include "reboot/integration/purge_scope.hpp"
#include "reboot/integration/purge_targets.hpp"

namespace rb {
class AppLayout;
class Executor;
class WorkerPool;
struct InstallLayout;
}  // namespace rb

namespace rb::ports {
class IFileSystem;
}

namespace rb::integration {

// Covers no capability ids (decision os-integration-ledger, its PurgeScope).
// Strand-only; deletion runs on the WorkerPool. Nothing else removes an AppImage or tarball user's data.
class PurgeService {
public:
    PurgeService(ports::IFileSystem& fs, const AppLayout& layout, const InstallLayout& install, PurgeTargets targets,
                 PurgeHooks hooks, WorkerPool& workers, Executor& strand, OpRegistry& ops);
    PurgeService(const PurgeService&) = delete;
    PurgeService& operator=(const PurgeService&) = delete;

    // OpKind::Generic, completing with a PurgeReport; any blocker is integration.purge_blocked, with no op.
    // A purge in progress blocks another; blockers found once `prepare` is ready fail the op.
    Result<OpHandle> start_purge(PurgeScope scope, DisconnectPolicy policy);

private:
    void prepared(PurgeScope scope, Operation<PurgeReport>& op);
    void finish(PurgeScope scope, Operation<PurgeReport>& op, Outcome<PurgeReport> outcome);

    ports::IFileSystem& fs_;
    const AppLayout& layout_;
    const InstallLayout& install_;
    PurgeTargets targets_;
    PurgeHooks hooks_;
    WorkerPool& workers_;
    Executor& strand_;
    OpRegistry& ops_;
    std::optional<OpId> running_;
};

}  // namespace rb::integration
