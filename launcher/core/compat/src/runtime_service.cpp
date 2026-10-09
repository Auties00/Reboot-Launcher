#include "reboot/compat/runtime_service.hpp"

#include <string_view>
#include <utility>

#include "reboot/components/component_store.hpp"
#include "reboot/components/manifest_service.hpp"
#include "runtime_core.hpp"

namespace rb::compat {

namespace {

class StoreCatalog final : public RuntimeCatalog {
public:
    StoreCatalog(components::ComponentStore& store, components::ManifestService& manifest)
        : store_(store), manifest_(manifest) {}

    [[nodiscard]] std::vector<components::RuntimeEntry> runtimes() const override { return manifest_.runtimes(); }

    void acquire(SessionId session, std::string_view runtime_id, CancelToken token, components::ProgressSink progress,
                 UniqueFunction<void(Result<components::PinnedRuntime>)> done) override {
        store_.acquire_runtime(session, runtime_id, std::move(token), std::move(progress), std::move(done));
    }

    void mark_good(const components::ComponentPin& pin) override { store_.mark_good(pin); }

private:
    components::ComponentStore& store_;
    components::ManifestService& manifest_;
};

}  // namespace

struct RuntimeService::Impl {
    explicit Impl(RuntimeServiceDeps deps)
        : catalog(deps.store, deps.manifest),
          core(RuntimeCoreDeps{catalog, deps.runner, deps.prefixes, deps.requests, deps.ops, deps.workers, deps.strand,
                               deps.clock, deps.document}) {}

    StoreCatalog catalog;
    RuntimeCore core;
};

RuntimeService::RuntimeService(RuntimeServiceDeps deps) : impl_(std::make_unique<Impl>(deps)) {}

RuntimeService::~RuntimeService() = default;

std::vector<RunnerKind> RuntimeService::supported() const { return impl_->core.supported(); }

Result<RunnerProfile> RuntimeService::profile(RunnerKind kind) const { return impl_->core.profile(kind); }

void RuntimeService::prepare(SessionId session, const RunnerProfile& profile, OperationBase& op,
                             components::ProgressSink progress, UniqueFunction<void(Result<PreparedRuntime>)> done) {
    impl_->core.prepare(session, profile, op, std::move(progress), std::move(done));
}

VcRedistSource RuntimeService::vc_redist_source(SessionId session, CancelToken token) {
    return impl_->core.vc_redist_source(session, std::move(token));
}

Result<OpHandle> RuntimeService::start_setup(RunnerKind kind, DisconnectPolicy policy) {
    return impl_->core.start_setup(kind, policy);
}

void RuntimeService::mark_good(const PreparedRuntime& runtime) { impl_->core.mark_good(runtime); }

}  // namespace rb::compat
