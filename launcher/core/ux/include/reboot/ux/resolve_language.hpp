#pragma once

#include <optional>
#include <span>

#include "reboot/foundation/types.hpp"
#include "reboot/ux/language_info.hpp"
#include "reboot/ux/language_preference.hpp"
#include "reboot/ux/language_tag.hpp"

namespace reboot::ux {

enum class LanguageSource : u8 { Explicit, Os, Default };

struct ResolvedLanguage {
    LanguageTag tag = LanguageTag::english();
    LanguageSource source = LanguageSource::Default;
    bool rtl = false;
    // The explicit preference when no shipped catalog matched it; it stays stored as written.
    std::optional<LanguageTag> unavailable_explicit;
};

// Capabilities: localization.strings-and-language, localization.+48.
// RFC 4647 Lookup over [explicit tag, `os_preferred` in order, en] against `shipped`, at each use.
[[nodiscard]] ResolvedLanguage resolve_language(const LanguagePreference& preference,
                                                std::span<const LanguageTag> os_preferred,
                                                std::span<const LanguageInfo> shipped);

}  // namespace reboot::ux
