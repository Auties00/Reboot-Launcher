#include "reboot/catalog/alias_table.hpp"

#include <algorithm>

namespace rb::catalog {

namespace {

[[nodiscard]] std::string ascii_lower(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

[[nodiscard]] constexpr bool is_ascii_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && is_ascii_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_ascii_space(text.back())) text.remove_suffix(1);
    return text;
}

}  // namespace

AliasTable::AliasTable() : AliasTable(for_catalog(Catalog{})) {}

AliasTable AliasTable::for_catalog(const Catalog& catalog) {
    std::vector<std::pair<std::string, AliasMatch>> names;
    const auto add = [&](std::string_view name, const CatalogEntry& entry, AliasSource source) {
        // A blank name could only ever match a blank query.
        if (const std::string_view key = trim(name); !key.empty())
            names.emplace_back(ascii_lower(key), AliasMatch{entry.id, entry.version, source});
    };
    for (const auto& entry : catalog.entries) {
        add(entry.id, entry, AliasSource::CatalogId);
        for (const auto& alias : entry.aliases) add(alias, entry, AliasSource::CatalogAlias);
    }

    // Stable, so among equal names and sources the first in catalog order wins.
    std::ranges::stable_sort(names, [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second.source < b.second.source;
    });
    const auto duplicates = std::ranges::unique(names, {}, &std::pair<std::string, AliasMatch>::first);
    names.erase(duplicates.begin(), duplicates.end());

    return AliasTable(std::move(names));
}

std::optional<AliasMatch> AliasTable::resolve(std::string_view name) const {
    const std::string key = ascii_lower(trim(name));
    const auto it = std::ranges::lower_bound(names_, key, {}, &std::pair<std::string, AliasMatch>::first);
    if (it == names_.end() || it->first != key) return std::nullopt;
    return it->second;
}

}  // namespace rb::catalog
