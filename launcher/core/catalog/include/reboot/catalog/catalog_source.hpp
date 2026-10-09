#pragma once

#include <expected>
#include <vector>

#include "reboot/catalog/catalog.hpp"
#include "reboot/catalog/catalog_error.hpp"
#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::catalog {

enum class CatalogOrigin : u8 { Remote, Cache, Bundled };

// CacheOnly never touches the network; sources without a network side treat both alike.
enum class CatalogFetch : u8 { CacheOnly, Revalidate };

struct LoadedCatalog {
    Catalog catalog;
    CatalogOrigin origin = CatalogOrigin::Bundled;
    // Non-fatal findings, such as a failed cache write.
    std::vector<Diagnostic> warnings;
};

using CatalogLoadResult = std::expected<LoadedCatalog, CatalogError>;

// Capabilities: game-builds.catalog, game-builds.historical-remote-catalog.
// One provider of a whole verified catalog document. Copies from different sources are never
// merged by build id: the serial and rollback checks hold only for one signed document.
class ICatalogSource {
public:
    virtual ~ICatalogSource() = default;

    // `done` runs on the strand exactly once, also after a cancel. The next load starts only after it ran.
    virtual void load(CatalogFetch fetch, CancelToken token, UniqueFunction<void(CatalogLoadResult)> done) = 0;
};

}  // namespace rb::catalog
