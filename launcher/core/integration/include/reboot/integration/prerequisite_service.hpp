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
    Result<OpHandle> start_remediate(PrerequisiteId id, DisconnectPolicy policy);

private:
    ports::IPrerequisiteProbe& probe_;
    WorkerPool& workers_;
    Executor& strand_;
    OpRegistry& ops_;
    EventBus& events_;
};

}  // namespace reboot::integration
