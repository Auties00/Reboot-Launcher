#include "reboot/engine/engine_stores.hpp"

#include <memory>
#include <utility>

#include "reboot/foundation/events.hpp"
#include "reboot/foundation/executor.hpp"

namespace reboot::engine {

EngineStores::EngineStores(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, const IClock& clock,
                           const AppLayout& layout)
    : settings(fs, workers, strand, clock, layout.settings_file()),
      library(fs, workers, strand, clock, layout.library_file()),
      accounts(fs, workers, strand, clock, layout.accounts_file()),
      backend_logins(fs, workers, strand, clock, identity::backend_logins_document_path(layout)),
      host_profiles(fs, workers, strand, clock, layout.host_profiles_file()),
      state(fs, workers, strand, clock, layout.state_file()),
      runtime(fs, workers, strand, clock, layout.runtime_file()),
      resume(fs, workers, strand, clock, layout.resume_file()),
      compat(fs, workers, strand, clock, compat::compat_document_path(layout)),
      describe_cache(fs, workers, strand, clock, layout.describe_cache()) {}

EngineStores::~EngineStores() = default;

namespace {

template <class Store>
storage::LoadReport load_one(Store& store, const std::optional<Diagnostic>& memory_only) {
    return memory_only ? store.load_memory_only(*memory_only) : store.load();
}

}  // namespace

std::vector<storage::LoadReport> EngineStores::load(std::optional<Diagnostic> memory_only) {
    std::vector<storage::LoadReport> reports;
    reports.push_back(load_one(settings, memory_only));
    reports.push_back(load_one(library, memory_only));
    reports.push_back(load_one(accounts, memory_only));
    reports.push_back(load_one(backend_logins, memory_only));
    reports.push_back(load_one(host_profiles, memory_only));
    reports.push_back(load_one(state, memory_only));
    reports.push_back(load_one(runtime, memory_only));
    reports.push_back(load_one(resume, memory_only));
    reports.push_back(load_one(compat, memory_only));
    reports.push_back(load_one(describe_cache, memory_only));
    mode_ = storage::combined_mode(reports);
    return reports;
}

void EngineStores::flush_all(UniqueFunction<void(Result<void>)> done) {
    struct Join {
        std::size_t left = 0;
        std::optional<Diagnostic> first_error;
        UniqueFunction<void(Result<void>)> done;
    };
    auto join = std::make_shared<Join>();
    join->left = 10;
    join->done = std::move(done);
    auto one = [join](Result<void> flushed) {
        if (!flushed && !join->first_error) join->first_error = std::move(flushed.error());
        if (--join->left > 0) return;
        if (join->first_error) join->done(std::unexpected(std::move(*join->first_error)));
        else join->done(Result<void>{});
    };
    settings.flush(CancelToken{}, one);
    library.flush(CancelToken{}, one);
    accounts.flush(CancelToken{}, one);
    backend_logins.flush(CancelToken{}, one);
    host_profiles.flush(CancelToken{}, one);
    state.flush(CancelToken{}, one);
    runtime.flush(CancelToken{}, one);
    resume.flush(CancelToken{}, one);
    compat.flush(CancelToken{}, one);
    describe_cache.flush(CancelToken{}, one);
}

void EngineStores::publish_mode_changes(EventBus& events) {
    auto publish = [&events](const storage::StorageModeChanged& changed) {
        events.publish(EventKind::StorageModeChanged, changed, EventScope{.coalesce_key = changed.document});
    };
    settings.set_on_mode_changed(publish);
    library.set_on_mode_changed(publish);
    accounts.set_on_mode_changed(publish);
    backend_logins.set_on_mode_changed(publish);
    host_profiles.set_on_mode_changed(publish);
    state.set_on_mode_changed(publish);
    runtime.set_on_mode_changed(publish);
    resume.set_on_mode_changed(publish);
    compat.set_on_mode_changed(publish);
    describe_cache.set_on_mode_changed(publish);
}

}  // namespace reboot::engine
