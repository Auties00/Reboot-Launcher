#include "reboot/ux/resolve_language.hpp"

#include "language_lookup.hpp"

namespace reboot::ux {

namespace {

[[nodiscard]] const LanguageInfo* lookup(const LanguageTag& range, std::span<const LanguageInfo> shipped) {
    for (const std::string_view candidate : lookup_fallbacks(range.str()))
        for (const LanguageInfo& info : shipped)
            if (info.tag.str() == candidate) return &info;
    return nullptr;
}

}  // namespace

ResolvedLanguage resolve_language(const LanguagePreference& preference, std::span<const LanguageTag> os_preferred,
                                  std::span<const LanguageInfo> shipped) {
    ResolvedLanguage resolved;
    if (const std::optional<LanguageTag>& tag = preference.tag()) {
        if (const LanguageInfo* match = lookup(*tag, shipped)) {
            resolved.tag = match->tag;
            resolved.source = LanguageSource::Explicit;
            resolved.rtl = match->rtl;
            return resolved;
        }
        resolved.unavailable_explicit = *tag;
    }
    for (const LanguageTag& os : os_preferred) {
        if (const LanguageInfo* match = lookup(os, shipped)) {
            resolved.tag = match->tag;
            resolved.source = LanguageSource::Os;
            resolved.rtl = match->rtl;
            return resolved;
        }
    }
    if (const LanguageInfo* match = lookup(LanguageTag::english(), shipped)) resolved.rtl = match->rtl;
    return resolved;
}

}  // namespace reboot::ux
