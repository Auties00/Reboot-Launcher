#pragma once

#include <optional>
#include <vector>

#include "reboot/compat/compat_document.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/gameserver/describe_cache_document.hpp"
#include "reboot/host/host_profiles_document.hpp"
#include "reboot/identity/backend_logins_document.hpp"
#include "reboot/storage/accounts_document.hpp"
#include "reboot/storage/document_store.hpp"
#include "reboot/storage/library_document.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/resume_document.hpp"
#include "reboot/storage/runtime_document.hpp"
#include "reboot/storage/settings_document.hpp"
#include "reboot/storage/state_document.hpp"

namespace reboot {
class EventBus;
class Executor;
class IClock;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::engine {

// Capabilities: none. Every AppLayout document in one place; strand-only once loaded.
class EngineStores {
public:
    EngineStores(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, const IClock& clock,
                 const AppLayout& layout);
    EngineStores(const EngineStores&) = delete;
    EngineStores& operator=(const EngineStores&) = delete;
    ~EngineStores();

    // Blocking, step 4; nothing aborts it. With `memory_only` every store stays in memory.
    [[nodiscard]] std::vector<storage::LoadReport> load(std::optional<Diagnostic> memory_only);

    // The most restricted mode of the last load().
    [[nodiscard]] storage::StorageMode mode() const noexcept { return mode_; }

    // `done` runs on the strand with the first write error, if any.
    void flush_all(UniqueFunction<void(Result<void>)> done);

    // From then on every store's StorageModeChanged goes out as EventKind::StorageModeChanged.
    void publish_mode_changes(EventBus& events);

    storage::DocumentStore<storage::SettingsDocument> settings;
    storage::DocumentStore<storage::LibraryDocument> library;
    storage::DocumentStore<storage::AccountsDocument> accounts;
    storage::DocumentStore<identity::BackendLoginsDocument> backend_logins;
    storage::DocumentStore<host::HostProfilesDocument> host_profiles;
    storage::DocumentStore<storage::StateDocument> state;
    storage::DocumentStore<storage::RuntimeDocument> runtime;
    storage::DocumentStore<storage::ResumeDocument> resume;
    storage::DocumentStore<compat::CompatDocument> compat;
    storage::DocumentStore<gameserver::DescribeCacheDocument> describe_cache;

private:
    storage::StorageMode mode_ = storage::StorageMode::ReadWrite;
};

}  // namespace reboot::engine
