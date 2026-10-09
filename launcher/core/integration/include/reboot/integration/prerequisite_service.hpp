#pragma once

#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/integration/prerequisite.hpp"
#include "reboot/integration/prerequisite_id.hpp"

namespace reboot {
class EventBus;
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IPrerequisiteProbe;
}

namespace reboot::integration {

// Covers no capability ids (decisions macos-compat-layer, linux-compat-layer, windows-headless-hosting).
// Strand-only; the probe runs on the WorkerPool and `done` on the strand. A remediation's re-check
// publishes PrerequisitesChanged.
class PrerequisiteService {
public:
    PrerequisiteService(ports::IPrerequisiteProbe& probe, WorkerPool& workers, Executor& strand, OpRegistry& ops,
                        EventBus& events);
    PrerequisiteService(const PrerequisiteService&) = delete;
    PrerequisiteService& operator=(const PrerequisiteService&) = delete;

    // Ids this build does not know are dropped.
    void check(CancelToken token, UniqueFunction<void(std::vector<Prerequisite>)> done);

    // OpKind::Generic, 10 min, re-checked; the call is the consent, so compat's RosettaInstall is the only request.
    // Completes with the re-checked Prerequisite. A remedy that only opens settings never ends in
    // StillMissing; one already met is not run again. A second call for an id in progress joins it,
    // and one after a cancel waits until the cancelled remedy has returned.
    Result<OpHandle> start_remediate(PrerequisiteId id, DisconnectPolicy policy);

private:
    struct Remediation {
        PrerequisiteId id{};
        OpHandle handle;
        // Listed until its remedy returns, even once the op was cancelled.
        Operation<Prerequisite>* op = nullptr;
    };

    void submit(const Remediation& remediation);
    void start_waiting(PrerequisiteId id);

    ports::IPrerequisiteProbe& probe_;
    WorkerPool& workers_;
    Executor& strand_;
    OpRegistry& ops_;
    EventBus& events_;
    std::vector<Remediation> running_;
};

}  // namespace reboot::integration
