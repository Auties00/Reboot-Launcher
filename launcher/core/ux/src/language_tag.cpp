#include "reboot/ux/language_tag.hpp"

#include <array>
#include <cstddef>
#include <vector>

#include "ascii.hpp"
#include "messages.hpp"

namespace rb::ux {

namespace {

// RFC 5646 irregular grandfathered tags, in their registry case; the regular ones fit the grammar.
constexpr std::array<std::string_view, 17> kIrregular{
    "en-GB-oed", "i-ami",     "i-bnn",   "i-default", "i-enochian", "i-hak",     "i-klingon", "i-lux",    "i-mingo",
    "i-navajo",  "i-pwn",     "i-tao",   "i-tay",     "i-tsu",      "sgn-BE-FR", "sgn-BE-NL", "sgn-CH-DE"};

[[nodiscard]] std::vector<std::string_view> split_subtags(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t dash = text.find('-', start);
        out.push_back(text.substr(start, dash == std::string_view::npos ? std::string_view::npos : dash - start));
        if (dash == std::string_view::npos) return out;
        start = dash + 1;
    }
}

[[nodiscard]] bool is_variant(std::string_view s) {
    if (!all_alnum(s)) return false;
    return (s.size() >= 5 && s.size() <= 8) || (s.size() == 4 && is_digit(s[0]));
}

// The canonical form of a well-formed tag, or nullopt.
[[nodiscard]] std::optional<std::string> canonicalize(std::string_view text) {
    if (text.empty()) return std::nullopt;
    for (const char c : text)
        if (!is_alnum(c) && c != '-') return std::nullopt;
    for (const std::string_view irregular : kIrregular)
        if (iequals(irregular, text)) return std::string(irregular);

    const std::vector<std::string_view> subtags = split_subtags(text);
    for (const std::string_view s : subtags)
        if (s.empty() || s.size() > 8) return std::nullopt;

    std::vector<std::string> out;
    std::size_t i = 0;
    const auto take_private_use = [&]() -> bool {
        out.emplace_back("x");
        ++i;
        if (i == subtags.size()) return false;
        for (; i < subtags.size(); ++i) out.push_back(to_lower(subtags[i]));
        return true;
    };

    if (iequals(subtags[0], "x")) {
        if (!take_private_use()) return std::nullopt;
    } else {
        const std::string_view language = subtags[i];
        if (!all_alpha(language) || language.size() < 2) return std::nullopt;
        out.push_back(to_lower(language));
        ++i;
        if (language.size() <= 3)
            for (int extlangs = 0; extlangs < 3 && i < subtags.size() && subtags[i].size() == 3 && all_alpha(subtags[i]);
                 ++extlangs, ++i)
                out.push_back(to_lower(subtags[i]));
        if (i < subtags.size() && subtags[i].size() == 4 && all_alpha(subtags[i])) {
            out.push_back(to_title(subtags[i]));
            ++i;
        }
        if (i < subtags.size() && ((subtags[i].size() == 2 && all_alpha(subtags[i])) ||
                                   (subtags[i].size() == 3 && all_digit(subtags[i])))) {
            out.push_back(to_upper(subtags[i]));
            ++i;
        }
        std::vector<std::string> variants;
        for (; i < subtags.size() && is_variant(subtags[i]); ++i) {
            std::string variant = to_lower(subtags[i]);
            for (const std::string& seen : variants)
                if (seen == variant) return std::nullopt;
            variants.push_back(variant);
            out.push_back(std::move(variant));
        }
        std::string singletons;
        while (i < subtags.size() && subtags[i].size() == 1 && !iequals(subtags[i], "x")) {
            const char singleton = to_lower(subtags[i][0]);
            if (singletons.find(singleton) != std::string::npos) return std::nullopt;
            singletons.push_back(singleton);
            out.emplace_back(1, singleton);
            ++i;
            const std::size_t first = i;
            for (; i < subtags.size() && subtags[i].size() >= 2 && all_alnum(subtags[i]); ++i)
                out.push_back(to_lower(subtags[i]));
            if (i == first) return std::nullopt;
        }
        if (i < subtags.size() && iequals(subtags[i], "x") && !take_private_use()) return std::nullopt;
        if (i != subtags.size()) return std::nullopt;
    }

    std::string joined;
    for (const std::string& s : out) {
        if (!joined.empty()) joined.push_back('-');
        joined += s;
    }
    return joined;
}

// glibc @modifiers that name a script.
[[nodiscard]] std::optional<std::string_view> modifier_script(std::string_view modifier) {
    if (modifier == "latin") return "Latn";
    if (modifier == "cyrillic") return "Cyrl";
    if (modifier == "devanagari") return "Deva";
    return std::nullopt;
}

}  // namespace

Result<LanguageTag> LanguageTag::parse(std::string_view text) {
    std::optional<std::string> canonical = canonicalize(text);
    if (!canonical)
        return make_diag(ErrorDomain::Ux, msg::kMalformedLanguageTag)
            .arg("value", text)
            .kind(ErrorKind::InvalidInput)
            .fail();
    return LanguageTag(std::move(*canonical));
}

std::optional<LanguageTag> LanguageTag::from_os_locale(std::string_view locale) {
    std::string_view base = locale;
    std::string_view modifier;
    if (const std::size_t at = base.find('@'); at != std::string_view::npos) {
        modifier = base.substr(at + 1);
        base = base.substr(0, at);
    }
    if (const std::size_t dot = base.find('.'); dot != std::string_view::npos) base = base.substr(0, dot);
    if (base.empty() || base == "C" || base == "POSIX") return std::nullopt;

    std::string text(base);
    for (char& c : text)
        if (c == '_') c = '-';
    const std::size_t first_dash = text.find('-');
    const std::string_view after_language =
        first_dash == std::string::npos ? std::string_view{} : std::string_view(text).substr(first_dash + 1);
    const bool has_script = !after_language.empty() && after_language.substr(0, after_language.find('-')).size() == 4;
    if (const std::optional<std::string_view> script = modifier_script(modifier); script && !has_script)
        text.insert(first_dash == std::string::npos ? text.size() : first_dash, "-" + std::string(*script));
    else if (modifier == "valencia")
        text += "-valencia";

    std::optional<std::string> canonical = canonicalize(text);
    if (!canonical) return std::nullopt;
    return LanguageTag(std::move(*canonical));
}

}  // namespace rb::ux
