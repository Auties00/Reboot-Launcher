#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "reboot/compat/compat_document.hpp"
#include "reboot/compat/prepared_runtime.hpp"
#include "reboot/compat/runner_profile.hpp"
#include "reboot/compat/vc_redist_source.hpp"
#include "reboot/components/component_pin.hpp"
#include "reboot/components/pinned_runtime.hpp"
#include "reboot/components/progress_sink.hpp"
#include "reboot/components/runtime_entry.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/document_store.hpp"

namespace rb {
class Executor;
class IClock;
class UserRequestRegistry;
class WorkerPool;
}  // namespace rb

namespace rb::ports {
class IRunnerPlatform;
}

namespace rb::compat {

class PrefixManager;

// What RuntimeService needs of the manifest and the component store; tests stand in for both.
class RuntimeCatalog {
public:
    virtual ~RuntimeCatalog() = default;

    // The runtimes the manifest selects for this platform.
    [[nodiscard]] virtual std::vector<components::RuntimeEntry> runtimes() const = 0;
    virtual void acquire(SessionId session, std::string_view runtime_id, CancelToken token,
                         components::ProgressSink progress,
                         UniqueFunction<void(Result<components::PinnedRuntime>)> done) = 0;
    virtual void mark_good(const components::ComponentPin& pin) = 0;
};

struct RuntimeCoreDeps {
    RuntimeCatalog& catalog;
    ports::IRunnerPlatform& runner;
    PrefixManager& prefixes;
    UserRequestRegistry& requests;
    OpRegistry& ops;
    WorkerPool& workers;
    Executor& strand;
    const IClock& clock;
    storage::DocumentStore<CompatDocument>& document;
};

// RuntimeService's behaviour over a RuntimeCatalog; runtime_service.hpp documents each member.
class RuntimeCore {
public:
    explicit RuntimeCore(RuntimeCoreDeps deps);
    ~RuntimeCore();
    RuntimeCore(const RuntimeCore&) = delete;
    RuntimeCore& operator=(const RuntimeCore&) = delete;

    [[nodiscard]] std::vector<RunnerKind> supported() const;
    [[nodiscard]] Result<RunnerProfile> profile(RunnerKind kind) const;
    void prepare(SessionId session, const RunnerProfile& profile, OperationBase& op, components::ProgressSink progress,
                 UniqueFunction<void(Result<PreparedRuntime>)> done);
    [[nodiscard]] VcRedistSource vc_redist_source(SessionId session, CancelToken token);
    [[nodiscard]] Result<OpHandle> start_setup(RunnerKind kind, DisconnectPolicy policy);
    void mark_good(const PreparedRuntime& runtime);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::compat
