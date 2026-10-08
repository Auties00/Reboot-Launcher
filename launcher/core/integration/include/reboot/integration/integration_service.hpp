#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/version.hpp"
#include "reboot/integration/entry_status.hpp"
#include "reboot/integration/integration_kind.hpp"
#include "reboot/integration/integration_targets.hpp"
#include "reboot/integration/reconcile_report.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/state_document.hpp"

namespace reboot {
class EventBus;
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IIntegrationRegistrar;
}

namespace reboot::integration {

// Covers os-integration.url-protocol and os-integration.+36.
// Strand-only; entries are read each time, Foreign ones never touched, and `done` runs on the strand.
// Apply, remove and a reconcile that changed anything publish IntegrationChanged.
class IntegrationService {
public:
    IntegrationService(ports::IIntegrationRegistrar& registrar, storage::DocumentStore<storage::StateDocument>& state,
                       WorkerPool& workers, Executor& strand, OpRegistry& ops, EventBus& events,
                       IntegrationTargets targets);
    IntegrationService(const IntegrationService&) = delete;
    IntegrationService& operator=(const IntegrationService&) = delete;

    void status(CancelToken token, UniqueFunction<void(std::vector<EntryStatus>)> done);

    // OpKind::Generic with each kind's EntryStatus, a failure in its `detail`; clears the declined flags.
    Result<OpHandle> start_apply(std::vector<IntegrationKind> kinds, DisconnectPolicy policy);

    // As start_apply, removing only entries of ours and setting their declined flags.
    Result<OpHandle> start_remove(std::vector<IntegrationKind> kinds, DisconnectPolicy policy);

    // After the endpoint opens; nullopt once `running` ran, which is recorded even if a write fails.
    void reconcile(SemVer running, CancelToken token, UniqueFunction<void(std::optional<ReconcileReport>)> done);

private:
    ports::IIntegrationRegistrar& registrar_;
    storage::DocumentStore<storage::StateDocument>& state_;
    WorkerPool& workers_;
    Executor& strand_;
    OpRegistry& ops_;
    EventBus& events_;
    IntegrationTargets targets_;
};

}  // namespace reboot::integration
