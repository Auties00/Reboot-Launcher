#pragma once

#include <span>

#include "reboot/ux/language_tag.hpp"

namespace reboot::ux {

// Capabilities: localization.strings-and-language, drop-features.
// A language a UI can offer; the CLI falls back to en for an rtl one, since terminals disagree on BiDi.
struct LanguageInfo {
    LanguageTag tag = LanguageTag::english();
    bool rtl = false;
    // False for an explicit preference that no catalog ships for; UIs show it as unavailable.
    bool shipped = true;
};

// v1 ships en only.
[[nodiscard]] std::span<const LanguageInfo> shipped_languages();

// By script subtag (Arab, Hebr, Thaa, Syrc, Nkoo, Adlm, Rohg), else by primary language.
[[nodiscard]] bool is_rtl(const LanguageTag& tag);

[[nodiscard]] LanguageInfo describe_language(const LanguageTag& tag);

}  // namespace reboot::ux
