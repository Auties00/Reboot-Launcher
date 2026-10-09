#include "reboot/catalog/catalog.hpp"

#include <algorithm>

namespace rb::catalog {

const CatalogEntry* Catalog::find(std::string_view id) const noexcept {
    const auto it = std::ranges::find(entries, id, &CatalogEntry::id);
    return it == entries.end() ? nullptr : &*it;
}

std::vector<const CatalogEntry*> Catalog::installable_entries() const {
    std::vector<const CatalogEntry*> out;
    for (const auto& entry : entries)
        if (entry.installable()) out.push_back(&entry);
    return out;
}

BuildFlags Catalog::flags_for(const GameVersion& version) const noexcept {
    for (const auto& range : flag_ranges)
        if (range.versions.contains(version)) return range.flags;
    return {};
}

}  // namespace rb::catalog
