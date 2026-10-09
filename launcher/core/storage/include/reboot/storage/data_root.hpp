#pragma once

#include <optional>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/storage/load_report.hpp"

namespace rb::ports {
class IFileSystem;
class IPlatformPaths;
}  // namespace rb::ports

namespace rb::storage {

struct DataRootReport {
    // InMemory when a directory could not be created; every store then loads memory-only.
    StorageMode mode = StorageMode::ReadWrite;
    std::optional<Diagnostic> reason;
    // Features that need a missing bundled file fail when used, not at start.
    std::vector<NativePath> missing_install_files;
};

// Capabilities: settings-storage.install-paths, settings-storage.layout.
// Blocking, at startup. Fails only with storage.root_inside_package, since updates replace that dir.
[[nodiscard]] Result<DataRootReport> prepare_data_root(const AppLayout& layout, const InstallLayout& install,
                                                       const ports::IPlatformPaths& paths, ports::IFileSystem& fs);

}  // namespace rb::storage
