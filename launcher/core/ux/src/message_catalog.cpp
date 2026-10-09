#include "reboot/ux/message_catalog.hpp"

#include <algorithm>

#include "reboot/foundation/diag.hpp"

namespace reboot::ux {

MessageCatalog MessageCatalog::english_from_registry() {
    std::vector<std::pair<std::string, std::string>> entries;
    for (const MessageSpec* spec : message_registry()) entries.emplace_back(spec->id, spec->english);
    std::ranges::stable_sort(entries, {}, &std::pair<std::string, std::string>::first);
    const auto duplicates = std::ranges::unique(entries, {}, &std::pair<std::string, std::string>::first);
    entries.erase(duplicates.begin(), duplicates.end());
    return MessageCatalog(LanguageTag::english(), std::move(entries));
}

std::optional<std::string_view> MessageCatalog::text(std::string_view id) const {
    const auto it = std::ranges::lower_bound(entries_, id, {}, [](const auto& entry) { return std::string_view(entry.first); });
    if (it == entries_.end() || it->first != id) return std::nullopt;
    return it->second;
}

}  // namespace reboot::ux
