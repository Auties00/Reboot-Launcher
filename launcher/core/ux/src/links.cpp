#include "links.hpp"

#include "language_lookup.hpp"

namespace reboot::ux {

std::string localized_url(std::span<const LocalizedUrl> urls, const LanguageTag& language) {
    for (const std::string_view range : lookup_fallbacks(language.str()))
        for (const LocalizedUrl& entry : urls)
            if (entry.language == range) return std::string(entry.url);
    for (const LocalizedUrl& entry : urls)
        if (entry.language == "en") return std::string(entry.url);
    return {};
}

}  // namespace reboot::ux
