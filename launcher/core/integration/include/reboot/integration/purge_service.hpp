#pragma once

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/integration/purge_hooks.hpp"
#include "reboot/integration/purge_scope.hpp"
#include "reboot/integration/purge_targets.hpp"

namespace reboot {
class AppLayout;
class Executor;
class WorkerPool;
struct InstallLayout;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::integration {

// Covers no capability ids (decision os-integration-ledger, its PurgeScope).
// Strand-only; deletion runs on the WorkerPool. Nothing else removes an AppImage or tarball user's data.
class PurgeService {
public:
    PurgeService(ports::IFileSystem& fs, const AppLayout& layout, const InstallLayout& install, PurgeTargets targets,
                 PurgeHooks hooks, WorkerPool& workers, Executor& strand, OpRegistry& ops);
    PurgeService(const PurgeService&) = delete;
    PurgeService& operator=(const PurgeService&) = delete;

    // OpKind::Generic, completing with a PurgeReport; any blocker is integration.purge_blocked, with no op.
    Result<OpHandle> start_purge(PurgeScope scope, DisconnectPolicy policy);

private:
    ports::IFileSystem& fs_;
    const AppLayout& layout_;
    const InstallLayout& install_;
    PurgeTargets targets_;
    PurgeHooks hooks_;
    WorkerPool& workers_;
    Executor& strand_;
    OpRegistry& ops_;
};

}  // namespace reboot::integration
