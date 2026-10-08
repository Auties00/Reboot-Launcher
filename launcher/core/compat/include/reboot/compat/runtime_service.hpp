#pragma once

#include <memory>
#include <vector>

#include "reboot/compat/compat_document.hpp"
#include "reboot/compat/prepared_runtime.hpp"
#include "reboot/compat/runner_profile.hpp"
#include "reboot/compat/vc_redist_source.hpp"
#include "reboot/components/progress_sink.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/document_store.hpp"

namespace reboot {
class Executor;
class IClock;
class UserRequestRegistry;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IRunnerPlatform;
}

namespace reboot::components {
class ComponentStore;
class ManifestService;
}  // namespace reboot::components

namespace reboot::compat {

class PrefixManager;

struct RuntimeServiceDeps {
    components::ComponentStore& store;
    components::ManifestService& manifest;
    ports::IRunnerPlatform& runner;
    PrefixManager& prefixes;
    UserRequestRegistry& requests;
    OpRegistry& ops;
    WorkerPool& workers;
    Executor& strand;
    const IClock& clock;
    storage::DocumentStore<CompatDocument>& document;
};

// Covers no capability ids (decisions linux-compat-layer, macos-compat-layer, update-mechanism, owner-2).
// Strand-only; port calls that may block run on the WorkerPool. Play only: hosting runs our
// native game server and never reaches this service.
class RuntimeService {
public:
    explicit RuntimeService(RuntimeServiceDeps deps);
    ~RuntimeService();
    RuntimeService(const RuntimeService&) = delete;
    RuntimeService& operator=(const RuntimeService&) = delete;

    // IRunnerPlatform::supported(), best first.
    [[nodiscard]] std::vector<RunnerKind> supported() const;

    // The manifest's selected runtime of each RuntimeKind the runner needs; a pin moves only when
    // the manifest selects another. compat.runner_unsupported, or compat.no_runtime.
    [[nodiscard]] Result<RunnerProfile> profile(RunnerKind kind) const;

    // Play preflight, in order:
    // - takes the prefix lease; compat.runtime_setup_running while a setup op runs on the runner;
    // - MacRuntime: a missing Rosetta raises RosettaInstall and puts `op` in AwaitingUser until it
    //   is answered; Declined fails with compat.rosetta_declined;
    // - Umu: compat.runtime_setup_required until start_setup() completed with this launcher;
    // - pins every runtime through ComponentStore::acquire_runtime, fetching what is missing;
    // - runs the idempotent IRunnerPlatform::post_extract on each, since the store may have
    //   re-extracted one under the same id;
    // - resolves the layout from all the pinned runtimes at once as one RuntimeDirs (Umu: GE-Proton
    //   and umu-launcher).
    void prepare(SessionId session, const RunnerProfile& profile, OperationBase& op,
                 components::ProgressSink progress, UniqueFunction<void(Result<PreparedRuntime>)> done);

    // PrefixRequest::vc_redist: the manifest's RuntimeKind::VcRedist runtime, pinned to `session`.
    [[nodiscard]] VcRedistSource vc_redist_source(SessionId session, CancelToken token);

    // Components.runtime_setup: an OpKind::RuntimeSetup op whose Operation<std::vector<ComponentRef>>
    // completes with the runtimes it ensured.
    // It never runs inside a session: compat.runtime_in_use while the runner's prefix is not idle.
    // Umu installs or updates the Steam Linux Runtime through IRunnerPlatform::runtime_setup and
    // records the build it reports with the time; other runners complete once their runtimes are
    // ensured.
    [[nodiscard]] Result<OpHandle> start_setup(RunnerKind kind, DisconnectPolicy policy);

    // A session on `runtime` completed: the store may now collect the previous versions, and the
    // Rosetta first-run multiplier no longer applies.
    void mark_good(const PreparedRuntime& runtime);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::compat
