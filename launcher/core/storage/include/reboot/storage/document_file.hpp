#pragma once

#include <memory>
#include <string_view>

#include <boost/json/object.hpp>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/storage_mode_changed.hpp"

namespace rb {
class Executor;
class IClock;
class WorkerPool;
}  // namespace rb

namespace rb::ports {
class IFileSystem;
}

namespace rb::storage {

struct DocumentFormat {
    // Also names the <name>.v<N>.json schema backups.
    std::string_view name;
    u32 schema = 1;
    Result<boost::json::object> (*upgrade)(boost::json::object values, u32 from_schema) = nullptr;
};

// Capabilities: settings-storage.layout, settings-storage.+73, settings-storage.+85.
// Strand-only. One {schema, revision, values} file; a hand edit is reloaded under unwritten changes.
class DocumentFile {
public:
    DocumentFile(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, const IClock& clock, NativePath path,
                 DocumentFormat format);
    ~DocumentFile();
    DocumentFile(const DocumentFile&) = delete;
    DocumentFile& operator=(const DocumentFile&) = delete;

    // Blocking: engine startup only, before the strand runs.
    [[nodiscard]] LoadReport load();
    // For a data root that could not be created: defaults, and disk is never touched.
    [[nodiscard]] LoadReport load_memory_only(Diagnostic reason);

    [[nodiscard]] const boost::json::object& values() const noexcept;
    [[nodiscard]] u64 revision() const noexcept;
    [[nodiscard]] StorageMode mode() const noexcept;

    // Returns the new revision. In ReadOnly mode fails with the load's reason: storage.read_only,
    // storage.schema_backup_failed or storage.upgrade_failed.
    Result<u64> replace(boost::json::object values);

    // Reloads a hand edit made since the last write.
    void refresh();
    // `done` runs on the strand once all accepted changes are on disk; cancelling ends only the wait.
    void flush(CancelToken cancel, UniqueFunction<void(Result<void>)> done);

    // Runs on the strand after a hand edit was reloaded.
    void set_on_reload(UniqueFunction<void(const boost::json::object& values)> on_reload);
    // Runs on the strand when a write fails, or succeeds after a failure.
    void set_on_mode_changed(UniqueFunction<void(const StorageModeChanged&)> on_mode_changed);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::storage
