#pragma once

#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/foundation/paths.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::components {

// What the app package carries for the core. aria2c, 7-Zip and WinRAR are not shipped: downloads
// and extraction run in process, and the payload and runtimes come through the ComponentStore.
enum class BundledAsset : u8 {
    BackendExe,
    GameServerExe,
    BackendContent,
    BundledCatalog,
    BundledManifest,
    // The bundled manifest's detached ".sig", beside it.
    BundledManifestSignature,
};

struct MissingAsset {
    BundledAsset asset{};
    NativePath path;
};

[[nodiscard]] NativePath bundled_asset_path(const InstallLayout& install, BundledAsset asset);

// Capabilities: packaging-distribution.assets.
// Blocking; runs on the WorkerPool. Empty when the install is complete.
[[nodiscard]] Result<std::vector<MissingAsset>> find_missing_assets(ports::IFileSystem& fs,
                                                                    const InstallLayout& install);

}  // namespace reboot::components
