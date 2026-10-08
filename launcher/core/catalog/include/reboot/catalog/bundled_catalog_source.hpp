#pragma once

#include "reboot/catalog/catalog_source.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/native_path.hpp"

namespace reboot {
class Executor;
class WorkerPool;
struct InstallLayout;
}  // namespace reboot

namespace reboot::ports {
class IFileSystem;
}

namespace reboot::trust {
class KeyRing;
}

namespace reboot::catalog {

// game-builds.catalog: the signed snapshot shipped at InstallLayout::bundled_catalog, with its
// .sig beside it. It is verified like a download and is the fallback when no cache verifies.
class BundledCatalogSource final : public ICatalogSource {
public:
    BundledCatalogSource(const InstallLayout& install, ports::IFileSystem& files, WorkerPool& workers,
                         Executor& strand, const trust::KeyRing& keys);

    // Every failure is BundledUnusable, which only a broken install produces.
    void load(CatalogFetch fetch, CancelToken token, UniqueFunction<void(CatalogLoadResult)> done) override;

private:
    NativePath body_;
    NativePath signature_;
    ports::IFileSystem& files_;
    WorkerPool& workers_;
    Executor& strand_;
    const trust::KeyRing& keys_;
};

}  // namespace reboot::catalog
