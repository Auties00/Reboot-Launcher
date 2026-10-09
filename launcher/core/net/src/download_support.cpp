#include "download_support.hpp"

#include <charconv>

#include "reboot/net/http_response.hpp"

namespace rb::net {

namespace {

constexpr std::string_view kSidecarVersion = "reboot-resume 1";

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

}  // namespace

std::optional<u64> parse_decimal(std::string_view value) {
    value = trim(value);
    if (value.empty()) return std::nullopt;
    u64 out = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || end != value.data() + value.size()) return std::nullopt;
    return out;
}

std::optional<ContentRange> parse_content_range(std::string_view value) {
    value = trim(value);
    constexpr std::string_view kUnit = "bytes ";
    if (!value.starts_with(kUnit)) return std::nullopt;
    value.remove_prefix(kUnit.size());
    const std::size_t dash = value.find('-');
    const std::size_t slash = value.find('/');
    if (dash == std::string_view::npos || slash == std::string_view::npos || dash > slash) return std::nullopt;
    const std::optional<u64> first = parse_decimal(value.substr(0, dash));
    const std::optional<u64> last = parse_decimal(value.substr(dash + 1, slash - dash - 1));
    if (!first || !last || *last < *first) return std::nullopt;
    ContentRange out{*first, *last, std::nullopt};
    const std::string_view total = trim(value.substr(slash + 1));
    if (total != "*") {
        out.total = parse_decimal(total);
        if (!out.total || *out.total <= *last) return std::nullopt;
    }
    return out;
}

std::string response_validator(const std::vector<ports::HttpHeader>& headers) {
    if (const std::string* etag = find_header(headers, "ETag")) {
        const std::string_view value = trim(*etag);
        if (!value.empty() && !value.starts_with("W/")) return std::string(value);
    }
    if (const std::string* modified = find_header(headers, "Last-Modified")) return std::string(trim(*modified));
    return {};
}

std::vector<u8> encode_sidecar(const ResumeSidecar& sidecar) {
    std::string text(kSidecarVersion);
    text += '\n';
    text += sidecar.url;
    text += '\n';
    text += sidecar.validator;
    text += '\n';
    text += sidecar.total ? std::to_string(*sidecar.total) : std::string("-");
    text += '\n';
    return {text.begin(), text.end()};
}

std::optional<ResumeSidecar> decode_sidecar(std::span<const u8> bytes) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) return std::nullopt;
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (lines.size() != 4 || lines[0] != kSidecarVersion || lines[1].empty() || lines[2].empty()) return std::nullopt;
    ResumeSidecar out{std::string(lines[1]), std::string(lines[2]), std::nullopt};
    if (lines[3] != "-") {
        out.total = parse_decimal(lines[3]);
        if (!out.total) return std::nullopt;
    }
    return out;
}

}  // namespace rb::net
