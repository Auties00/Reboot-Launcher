#pragma once

#include <utility>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/process/child_record.hpp"

namespace reboot {
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IProcessLauncher;
}

namespace reboot::process {

struct OrphanReapReport {
    std::vector<ChildRecord> killed;
    // Exited, or the pid now belongs to a process with another creation time.
    std::vector<ChildRecord> gone;
    // Still recorded, so the next start tries again.
    std::vector<std::pair<ChildRecord, Diagnostic>> failed;
};

// Covers no capability ids. Engine start step 5, before anything is spawned: kills the children a
// crashed engine left in state/runtime.json, but only where the pid and its creation time both
// still match, so a reused pid is never touched. Nothing is adopted. Matching a creation time can
// read /proc, so the is_alive and kill calls run on the WorkerPool; nothing else uses the launcher
// meanwhile, since nothing has been spawned yet.
class OrphanReaper {
public:
    OrphanReaper(ports::IProcessLauncher& launcher, WorkerPool& workers, Executor& strand) noexcept
        : launcher_(launcher), workers_(workers), strand_(strand) {}

    // `done` runs later on the strand, exactly once; it fails only with internal.bug. Once `token`
    // is cancelled, every record not yet checked is reported as failed with process.reap_cancelled.
    void reap(std::vector<ChildRecord> recorded, CancelToken token,
              UniqueFunction<void(Result<OrphanReapReport>)> done);

private:
    ports::IProcessLauncher& launcher_;
    WorkerPool& workers_;
    Executor& strand_;
};

}  // namespace reboot::process
