#include "reboot/foundation/text.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

#include "reboot/foundation/types.hpp"
#include "unicode_data.hpp"

namespace reboot {

namespace {

constexpr char32_t kReplacement = 0xFFFD;
constexpr std::size_t kMaxDetailBytes = 2048;

// Decodes the code point at `i` and advances past it. An invalid sequence yields nullopt and
// advances past its maximal subpart, so callers replace each subpart with one U+FFFD.
std::optional<char32_t> next_code_point(std::string_view text, std::size_t& i) {
    const auto byte = [&](std::size_t at) { return static_cast<u8>(text[at]); };
    const u8 lead = byte(i++);
    if (lead < 0x80) return lead;

    std::size_t extra = 0;
    char32_t cp = 0;
    u8 low = 0x80;
    u8 high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        cp = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        cp = lead & 0x0Fu;
        if (lead == 0xE0) low = 0xA0;
        if (lead == 0xED) high = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        cp = lead & 0x07u;
        if (lead == 0xF0) low = 0x90;
        if (lead == 0xF4) high = 0x8F;
    } else {
        return std::nullopt;
    }
    for (std::size_t k = 0; k < extra; ++k) {
        if (i >= text.size()) return std::nullopt;
        const u8 c = byte(i);
        if (c < low || c > high) return std::nullopt;
        low = 0x80;
        high = 0xBF;
        cp = (cp << 6) | (c & 0x3Fu);
        ++i;
    }
    return cp;
}

std::vector<char32_t> decode_lossy(std::string_view text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) out.push_back(next_code_point(text, i).value_or(kReplacement));
    return out;
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool is_stripped(char32_t cp) {
    // DEL counts as a control too: rbsb rejects it in every text field.
    if ((cp < 0x20 && cp != '\t') || (cp >= 0x7F && cp <= 0x9F)) return true;
    if (cp >= 0x202A && cp <= 0x202E) return true;
    if (cp >= 0x2066 && cp <= 0x2069) return true;
    return cp == 0x200B || cp == 0x2060 || cp == 0xFEFF;
}

constexpr char32_t kHangulSBase = 0xAC00;
constexpr char32_t kHangulLBase = 0x1100;
constexpr char32_t kHangulVBase = 0x1161;
constexpr char32_t kHangulTBase = 0x11A7;
constexpr char32_t kHangulLCount = 19;
constexpr char32_t kHangulVCount = 21;
constexpr char32_t kHangulTCount = 28;
constexpr char32_t kHangulNCount = kHangulVCount * kHangulTCount;
constexpr char32_t kHangulSCount = kHangulLCount * kHangulNCount;

u8 combining_class(char32_t cp) {
    const auto it = std::ranges::upper_bound(unicode::kCombiningRanges, static_cast<u32>(cp), {},
                                             &unicode::CombiningRange::first);
    if (it == unicode::kCombiningRanges.begin()) return 0;
    const auto& range = *std::prev(it);
    return cp <= range.last ? range.combining_class : u8{0};
}

void decompose(char32_t cp, std::vector<char32_t>& out) {
    if (cp >= kHangulSBase && cp < kHangulSBase + kHangulSCount) {
        const char32_t index = cp - kHangulSBase;
        out.push_back(kHangulLBase + index / kHangulNCount);
        out.push_back(kHangulVBase + (index % kHangulNCount) / kHangulTCount);
        if (const char32_t t = index % kHangulTCount; t != 0) out.push_back(kHangulTBase + t);
        return;
    }
    const auto it = std::ranges::lower_bound(unicode::kDecompositions, static_cast<u32>(cp), {},
                                             &unicode::Decomposition::code_point);
    if (it == unicode::kDecompositions.end() || it->code_point != cp) {
        out.push_back(cp);
        return;
    }
    decompose(it->first, out);
    if (it->second != 0) decompose(it->second, out);
}

std::optional<char32_t> compose_pair(char32_t first, char32_t second) {
    if (first >= kHangulLBase && first < kHangulLBase + kHangulLCount && second >= kHangulVBase &&
        second < kHangulVBase + kHangulVCount)
        return kHangulSBase + ((first - kHangulLBase) * kHangulVCount + (second - kHangulVBase)) * kHangulTCount;
    if (first >= kHangulSBase && first < kHangulSBase + kHangulSCount && (first - kHangulSBase) % kHangulTCount == 0 &&
        second > kHangulTBase && second < kHangulTBase + kHangulTCount)
        return first + (second - kHangulTBase);

    const auto key = [](const unicode::Composition& c) { return std::pair{c.first, c.second}; };
    const std::pair<u32, u32> wanted{static_cast<u32>(first), static_cast<u32>(second)};
    const auto it = std::ranges::lower_bound(unicode::kCompositions, wanted, {}, key);
    if (it == unicode::kCompositions.end() || key(*it) != wanted) return std::nullopt;
    return it->composite;
}

std::vector<char32_t> to_nfc(const std::vector<char32_t>& input) {
    std::vector<char32_t> out;
    out.reserve(input.size());
    for (const char32_t cp : input) decompose(cp, out);

    // Canonical ordering: a stable sort of each run of non-starters by combining class.
    for (std::size_t i = 0; i < out.size();) {
        if (combining_class(out[i]) == 0) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < out.size() && combining_class(out[end]) != 0) ++end;
        std::stable_sort(out.begin() + static_cast<std::ptrdiff_t>(i), out.begin() + static_cast<std::ptrdiff_t>(end),
                         [](char32_t a, char32_t b) { return combining_class(a) < combining_class(b); });
        i = end;
    }

    // Canonical composition.
    std::size_t starter = 0;
    bool have_starter = false;
    std::size_t write = 0;
    u8 last_class = 0;
    for (std::size_t read = 0; read < out.size(); ++read) {
        const char32_t cp = out[read];
        const u8 cls = combining_class(cp);
        if (have_starter) {
            const bool blocked = write > starter + 1 && (last_class == 0 || last_class >= cls);
            if (!blocked) {
                if (const auto composite = compose_pair(out[starter], cp)) {
                    out[starter] = *composite;
                    continue;
                }
            }
        }
        if (cls == 0) {
            starter = write;
            have_starter = true;
        }
        last_class = cls;
        out[write++] = cp;
    }
    out.resize(write);
    return out;
}

}  // namespace

bool is_valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();)
        if (!next_code_point(text, i)) return false;
    return true;
}

std::string sanitize_display_text(std::string_view text) {
    std::vector<char32_t> kept;
    kept.reserve(text.size());
    bool needs_normalising = false;
    for (const char32_t cp : decode_lossy(text)) {
        if (is_stripped(cp)) continue;
        // Below U+0300 nothing decomposes differently and nothing combines.
        if (cp >= 0x300) needs_normalising = true;
        kept.push_back(cp);
    }
    if (needs_normalising) kept = to_nfc(kept);

    std::string out;
    out.reserve(kept.size());
    for (const char32_t cp : kept) append_utf8(out, cp);
    return out;
}

std::string normalize_detail(std::string_view text) {
    std::string out = sanitize_display_text(text);
    if (out.size() <= kMaxDetailBytes) return out;
    std::size_t end = kMaxDetailBytes;
    while (end > 0 && (static_cast<u8>(out[end]) & 0xC0) == 0x80) --end;
    out.resize(end);
    return out;
}

std::u16string utf8_to_utf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const char32_t cp = next_code_point(text, i).value_or(kReplacement);
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            out.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        }
    }
    return out;
}

std::string utf16_to_utf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t unit = text[i];
        char32_t cp = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) + (static_cast<char32_t>(text[i + 1]) - 0xDC00);
            ++i;
        } else if (unit >= 0xD800 && unit <= 0xDFFF) {
            cp = kReplacement;
        }
        append_utf8(out, cp);
    }
    return out;
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return std::ranges::equal(a, b, {}, lower, lower);
}

}  // namespace reboot
