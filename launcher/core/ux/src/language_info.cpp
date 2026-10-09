#include "reboot/ux/language_info.hpp"

#include <algorithm>
#include <array>
#include <span>
#include <string_view>

#include "language_lookup.hpp"

namespace rb::ux {

namespace {

constexpr std::array<std::string_view, 7> kRtlScripts{"Arab", "Hebr", "Thaa", "Syrc", "Nkoo", "Adlm", "Rohg"};

// Languages whose likely script (CLDR) is right-to-left.
constexpr std::array<std::string_view, 23> kRtlLanguages{"ar",  "arc", "ckb", "dv", "fa",  "glk", "he",  "iw",
                                                         "ji",  "ks",  "lrc", "mzn", "nqo", "pnb", "prs", "ps",
                                                         "rhg", "sd",  "sdh", "syr", "ug",  "ur",  "yi"};

[[nodiscard]] bool contains(std::span<const std::string_view> set, std::string_view value) {
    return std::ranges::find(set, value) != set.end();
}

}  // namespace

std::span<const LanguageInfo> shipped_languages() {
    static const std::array<LanguageInfo, 1> kShipped{LanguageInfo{.tag = LanguageTag::english(), .rtl = false}};
    return kShipped;
}

bool is_rtl(const LanguageTag& tag) {
    const std::string_view text = tag.str();
    // Canonical case makes the script the only four-letter subtag that starts upper case.
    std::size_t start = text.find('-');
    while (start != std::string_view::npos) {
        const std::size_t end = text.find('-', start + 1);
        const std::string_view subtag = text.substr(start + 1, end == std::string_view::npos ? end : end - start - 1);
        if (subtag.size() == 1) break;
        if (subtag.size() == 4 && subtag[0] >= 'A' && subtag[0] <= 'Z') return contains(kRtlScripts, subtag);
        start = end;
    }
    return contains(kRtlLanguages, tag.primary_language());
}

LanguageInfo describe_language(const LanguageTag& tag) {
    bool shipped = false;
    for (const std::string_view range : lookup_fallbacks(tag.str()))
        for (const LanguageInfo& info : shipped_languages())
            shipped = shipped || info.tag.str() == range;
    return LanguageInfo{.tag = tag, .rtl = is_rtl(tag), .shipped = shipped};
}

}  // namespace rb::ux
