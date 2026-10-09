#include "reboot/compat/preferred_rhi_seed.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "reboot/foundation/text.hpp"

namespace rb::compat {

namespace {

constexpr std::string_view kSection = "[D3DRHIPreference]";
constexpr std::string_view kKey = "PreferredRHI";
constexpr std::string_view kSeededLine = "PreferredRHI=dx11";
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// One line split from its terminator, so rewriting the text keeps the terminator.
struct Line {
    std::string_view text;
    std::string_view end;
};

[[nodiscard]] std::vector<Line> split_lines(std::string_view ini) {
    std::vector<Line> lines;
    while (!ini.empty()) {
        const std::size_t newline = ini.find('\n');
        const std::size_t length = newline == std::string_view::npos ? ini.size() : newline + 1;
        std::string_view line = ini.substr(0, length);
        std::size_t text_length = line.size();
        if (text_length > 0 && line[text_length - 1] == '\n') --text_length;
        if (text_length > 0 && line[text_length - 1] == '\r') --text_length;
        lines.push_back({line.substr(0, text_length), line.substr(text_length)});
        ini.remove_prefix(length);
    }
    return lines;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

[[nodiscard]] bool is_section(std::string_view text) noexcept { return trim(text).starts_with('['); }

// The value of a PreferredRHI line; nullopt for any other line.
[[nodiscard]] std::optional<std::string_view> preferred_rhi_value(std::string_view text) {
    const std::size_t equals = text.find('=');
    if (equals == std::string_view::npos || !iequals_ascii(trim(text.substr(0, equals)), kKey)) return std::nullopt;
    return trim(text.substr(equals + 1));
}

[[nodiscard]] bool keeps_dxmt(std::string_view value) { return iequals_ascii(value, "dx11") || iequals_ascii(value, "dx10"); }

}  // namespace

std::optional<std::string> seed_preferred_rhi(std::string_view ini) {
    const std::string_view eol = ini.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::vector<Line> lines = split_lines(ini);
    if (!lines.empty() && lines.front().text.starts_with(kUtf8Bom)) lines.front().text.remove_prefix(kUtf8Bom.size());

    std::optional<std::size_t> section;
    bool has_key = false;
    std::vector<std::size_t> rewrite;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (is_section(lines[i].text)) {
            if (section) break;
            if (iequals_ascii(trim(lines[i].text), kSection)) section = i;
            continue;
        }
        if (!section) continue;
        if (const auto value = preferred_rhi_value(lines[i].text)) {
            has_key = true;
            if (!keeps_dxmt(*value)) rewrite.push_back(i);
        }
    }
    if (has_key && rewrite.empty()) return std::nullopt;

    std::string out;
    out.reserve(ini.size() + kSection.size() + kSeededLine.size() + 2 * eol.size());
    if (ini.starts_with(kUtf8Bom)) out += kUtf8Bom;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const bool rewritten = std::ranges::find(rewrite, i) != rewrite.end();
        out += rewritten ? kSeededLine : lines[i].text;
        if (section && *section == i && !has_key) {
            out += lines[i].end.empty() ? eol : lines[i].end;
            out += kSeededLine;
        }
        out += lines[i].end;
    }
    if (!section) {
        if (!lines.empty() && lines.back().end.empty()) out += eol;
        out += kSection;
        out += eol;
        out += kSeededLine;
        out += eol;
    }
    return out;
}

}  // namespace rb::compat
