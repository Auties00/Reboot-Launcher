#pragma once

#include <chrono>
#include <string_view>
#include <vector>

#include "reboot/catalog/build_flags.hpp"
#include "reboot/catalog/catalog_entry.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::catalog {

inline constexpr u32 kCatalogSchema = VersionStreams::catalog_schema;

// A verified catalog document. Ranges in `flag_ranges` never overlap and cover every entry's
// version; the parser rejects a document that breaks either rule.
struct Catalog {
    u32 schema = 0;
    u64 serial = 0;
    std::chrono::system_clock::time_point generated_at;
    std::chrono::system_clock::time_point expires_at;
    // Sorted by version, then id, so every UI shows the same order without sorting.
    std::vector<CatalogEntry> entries;
    std::vector<BuildFlagRange> flag_ranges;

    [[nodiscard]] const CatalogEntry* find(std::string_view id) const noexcept;

    // In `entries` order.
    [[nodiscard]] std::vector<const CatalogEntry*> installable_entries() const;

    // The defaults, so HotfixDelivery::Withhold, for a version no range covers. Only an imported
    // build outside the catalog can hit that, since the parser requires every entry covered.
    [[nodiscard]] BuildFlags flags_for(const GameVersion& version) const noexcept;

    bool operator==(const Catalog&) const = default;
};

}  // namespace rb::catalog
