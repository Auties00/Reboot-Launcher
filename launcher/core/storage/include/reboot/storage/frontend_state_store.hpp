#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/storage/load_report.hpp"
#include "reboot/storage/shell_name.hpp"

namespace reboot {
class Executor;
class WorkerPool;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::storage {

inline constexpr std::size_t kFrontendStateMaxBytes = 256u << 10;

// Capabilities: settings-storage.app-store, settings-storage.+52.
// Strand-only. One UTF-8 JSON blob per shell (window geometry) in config/frontend/<shell>.json.
class FrontendStateStore {
public:
    // In InMemory mode blobs live until the engine exits.
    FrontendStateStore(ports::IFileSystem& fs, WorkerPool& workers, Executor& strand, NativePath dir, StorageMode mode);
    ~FrontendStateStore();
    FrontendStateStore(const FrontendStateStore&) = delete;
    FrontendStateStore& operator=(const FrontendStateStore&) = delete;

    // A shell with nothing stored gets an empty blob. `done` runs on the strand.
    void get(const ShellName& shell, CancelToken cancel, UniqueFunction<void(Result<std::vector<u8>>)> done);
    // storage.frontend_state_too_large or _not_json; else `done` runs once the blob is on disk.
    void put(const ShellName& shell, std::vector<u8> blob, CancelToken cancel, UniqueFunction<void(Result<void>)> done);
    // `done` runs on the strand once every accepted put is on disk.
    void flush(CancelToken cancel, UniqueFunction<void(Result<void>)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace reboot::storage
