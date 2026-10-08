#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "reboot/catalog/catalog.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::catalog {

// Names any live mirror uses belong in the signed catalog's CatalogEntry::aliases.
enum class AliasSource : u8 { CatalogId, CatalogAlias };

struct AliasMatch {
    CatalogEntryId entry;
    GameVersion version;
    AliasSource source = AliasSource::CatalogId;

    bool operator==(const AliasMatch&) const = default;
};

// Covers no capability ids.
// Maps every name a catalog knows a build by to one canonical version, for version matching.
class AliasTable {
public:
    // Resolves nothing.
    AliasTable();

    // Precedence on a clash: catalog id, then catalog alias.
    [[nodiscard]] static AliasTable for_catalog(const Catalog& catalog);

    // ASCII case-insensitive and trimmed; no other normalisation, so "6.1.1" never becomes "6.10.1".
    [[nodiscard]] std::optional<AliasMatch> resolve(std::string_view name) const;

private:
    explicit AliasTable(std::vector<std::pair<std::string, AliasMatch>> names) : names_(std::move(names)) {}

    // Sorted by lowercased name, one match per name.
    std::vector<std::pair<std::string, AliasMatch>> names_;
};

}  // namespace reboot::catalog
