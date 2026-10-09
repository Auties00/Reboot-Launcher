#include "reboot/backend/backend_url.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

#include "messages.hpp"

namespace rb::backend {

namespace {

constexpr std::string_view kHttpPrefix = "http://";
constexpr std::string_view kHttpsPrefix = "https://";
constexpr std::size_t kMaxHostNameBytes = 253;
constexpr std::size_t kMaxLabelBytes = 63;

[[nodiscard]] char to_lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool starts_with_ignoring_case(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
           std::ranges::equal(text.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return to_lower_ascii(a) == b; });
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return text;
}

// RFC 1123 on an already lowercased name.
[[nodiscard]] bool is_host_name(std::string_view host) noexcept {
    if (host.empty() || host.size() > kMaxHostNameBytes) return false;
    std::size_t start = 0;
    while (start <= host.size()) {
        const std::size_t dot = std::min(host.find('.', start), host.size());
        const std::string_view label = host.substr(start, dot - start);
        if (label.empty() || label.size() > kMaxLabelBytes || label.front() == '-' || label.back() == '-') return false;
        const bool valid = std::ranges::all_of(label, [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        });
        if (!valid) return false;
        start = dot + 1;
    }
    return true;
}

// The text after the port colon, for backend.invalid_port.
[[nodiscard]] std::string_view port_text(std::string_view authority) noexcept {
    const std::size_t colon = authority.rfind(':');
    const std::size_t bracket = authority.rfind(']');
    if (colon == std::string_view::npos || (bracket != std::string_view::npos && colon < bracket)) return {};
    return authority.substr(colon + 1);
}

}  // namespace

Result<BackendUrl> BackendUrl::parse(std::string_view text) {
    std::string_view authority = trim(text);
    BackendUrl url;
    if (starts_with_ignoring_case(authority, kHttpsPrefix)) {
        url.scheme = net::UrlScheme::Https;
        authority.remove_prefix(kHttpsPrefix.size());
    } else if (starts_with_ignoring_case(authority, kHttpPrefix)) {
        url.scheme = net::UrlScheme::Http;
        authority.remove_prefix(kHttpPrefix.size());
    }
    if (!authority.empty() && authority.back() == '/') authority.remove_suffix(1);
    if (authority.find_first_of("/?#@") != std::string_view::npos)
        return invalid_input(msg::kInvalidUrl).arg("url", text).fail();

    const std::expected<HostPort, AddressError> parsed = parse_host_port(authority);
    if (!parsed) {
        if (parsed.error() == AddressError::BadPort)
            return invalid_input(msg::kInvalidPort).arg("port", port_text(authority)).fail();
        return invalid_input(msg::kInvalidUrl).arg("url", text).fail();
    }
    if (parsed->port && parsed->port->value == 0)
        return invalid_input(msg::kInvalidPort).arg("port", port_text(authority)).fail();

    std::string host = parsed->host;
    std::ranges::transform(host, host.begin(), to_lower_ascii);
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    if (!IpAddress::parse(host) && !is_host_name(host)) return invalid_input(msg::kInvalidUrl).arg("url", text).fail();

    url.host = std::move(host);
    url.port = parsed->port.value_or(kDefaultBackendPort);
    return url;
}

std::string BackendUrl::origin() const {
    std::string out = scheme == net::UrlScheme::Http ? std::string(kHttpPrefix) : std::string(kHttpsPrefix);
    const bool v6 = host.find(':') != std::string::npos;
    if (v6) out += '[';
    out += host;
    if (v6) out += ']';
    out += ':';
    out += std::to_string(port.value);
    return out;
}

}  // namespace rb::backend
