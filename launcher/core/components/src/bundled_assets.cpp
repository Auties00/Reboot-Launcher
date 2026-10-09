#include "reboot/components/bundled_assets.hpp"

#include <array>
#include <utility>

#include "messages.hpp"
#include "reboot/ports/file_system.hpp"

namespace reboot::components {

NativePath bundled_asset_path(const InstallLayout& install, BundledAsset asset) {
    switch (asset) {
        case BundledAsset::BackendExe: return install.backend_exe;
        case BundledAsset::GameServerExe: return install.game_server_exe;
        case BundledAsset::BackendContent: return install.backend_content_dir;
        case BundledAsset::BundledCatalog: return install.bundled_catalog;
        case BundledAsset::BundledManifest: return install.bundled_manifest;
        case BundledAsset::BundledManifestSignature: {
            NativePath signature = install.bundled_manifest;
            signature += ".sig";
            return signature;
        }
    }
    return {};
}

Diagnostic to_diagnostic(const MissingAsset& missing) {
    return make_diag(ErrorDomain::Components, kBundledAssetMissing)
        .arg("path", missing.path)
        .kind(ErrorKind::NotFound)
        .build();
}

Result<std::vector<MissingAsset>> find_missing_assets(ports::IFileSystem& fs, const InstallLayout& install) {
    constexpr std::array kAssets{BundledAsset::BackendExe,      BundledAsset::GameServerExe,
                                 BundledAsset::BackendContent,  BundledAsset::BundledCatalog,
                                 BundledAsset::BundledManifest, BundledAsset::BundledManifestSignature};
    std::vector<MissingAsset> missing;
    for (const BundledAsset asset : kAssets) {
        NativePath path = bundled_asset_path(install, asset);
        auto revision = fs.revision(path);
        if (revision) continue;
        // Anything but absence means the install cannot be judged, not that it is incomplete.
        if (revision.error().kind != ErrorKind::NotFound) return std::unexpected(std::move(revision.error()));
        missing.push_back(MissingAsset{asset, std::move(path)});
    }
    return missing;
}

}  // namespace reboot::components
