#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "reboot/catalog/alias_table.hpp"
#include "reboot/catalog/catalog.hpp"
#include "reboot/catalog/catalog_entry.hpp"
#include "reboot/catalog/catalog_source.hpp"
#include "reboot/catalog/catalog_updated.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/operation.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot {
class EventBus;
class IClock;
}  // namespace reboot

namespace reboot::catalog {

struct CatalogFilter {
    // The GUI lists installable entries; the CLI validates names against all of them.
    bool include_unavailable = false;
};

// IfExpired skips the network while the active copy has not expired.
enum class CatalogRefresh : u8 { IfExpired, Force };

// Capabilities: game-builds.catalog, game-builds.historical-remote-catalog.
// Owns the active catalog and its alias table, refreshes them as an op and publishes
// CatalogUpdated. Strand-only.
// - One whole copy is active, never a merge by build id, so the serial and rollback checks
//   cover everything listed. A manifest/chunk download would be an install strategy for catalog
//   entries, not a third source, so the two sources here are the complete set.
// - A copy with a lower serial than the active one is never activated.
// - current() and aliases() are replaced by every refresh; keep values, not references, across a
//   CatalogChanged.
class CatalogService {
public:
    CatalogService(ICatalogSource& remote, ICatalogSource& bundled, OpRegistry& ops, EventBus& events,
                   const IClock& clock);
    CatalogService(const CatalogService&) = delete;
    CatalogService& operator=(const CatalogService&) = delete;

    // Empty, with no origin, until the first refresh activates a copy.
    [[nodiscard]] const Catalog& current() const noexcept { return current_; }
    [[nodiscard]] std::optional<CatalogOrigin> origin() const noexcept { return origin_; }
    [[nodiscard]] const AliasTable& aliases() const noexcept { return aliases_; }

    // In Catalog::entries order: by version, then id.
    [[nodiscard]] std::vector<CatalogEntry> list(const CatalogFilter& filter) const;

    // Takes an id or any alias; fails with catalog.entry_not_found.
    [[nodiscard]] Result<CatalogEntry> entry(std::string_view name) const;
    // As entry(), and fails with catalog.entry_not_installable for an entry CatalogEntry::installable refuses.
    [[nodiscard]] Result<CatalogEntry> installable_entry(std::string_view name) const;

    // OpKind::HttpSmall, completing with the resulting CatalogUpdated. A call while one runs returns
    // the running op; once that op has ended, the call queues a new one behind it. With nothing
    // active it first activates the higher serial of the cache and the bundled copy, then
    // revalidates the remote unless `mode` is IfExpired and that copy has not expired. A remote
    // failure, unknown schema or rollback keeps the active copy and becomes a warning; the op fails
    // only when no copy is usable at all.
    Result<OpHandle> start_refresh(CatalogRefresh mode, DisconnectPolicy policy);

private:
    struct Refresh {
        OpHandle handle;
        Operation<CatalogUpdated>* op = nullptr;
        CatalogRefresh mode = CatalogRefresh::IfExpired;
        std::vector<Diagnostic> warnings;
        bool changed = false;
    };

    void run();
    void load_initial();
    void revalidate();
    void finish(std::optional<Diagnostic> failure);
    // Keeps the active copy when `loaded` has a lower serial.
    void activate(LoadedCatalog loaded);
    [[nodiscard]] CatalogUpdated describe(std::vector<Diagnostic> warnings) const;

    ICatalogSource& remote_;
    ICatalogSource& bundled_;
    OpRegistry& ops_;
    EventBus& events_;
    const IClock& clock_;
    Catalog current_;
    std::optional<CatalogOrigin> origin_;
    AliasTable aliases_;
    std::optional<Refresh> running_;
    std::optional<Refresh> queued_;
};

}  // namespace reboot::catalog
