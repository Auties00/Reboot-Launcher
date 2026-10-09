#include "url.hpp"

#include <algorithm>

#include "reboot/foundation/text.hpp"

namespace reboot::net {

namespace {

[[nodiscard]] char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

[[nodiscard]] std::string lowercase(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), lower);
    return out;
}

[[nodiscard]] bool is_name_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_';
}

}  // namespace

std::optional<ParsedUrl> parse_url(std::string_view url) {
    for (const char c : url)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return std::nullopt;

    const std::size_t separator = url.find("://");
    if (separator == std::string_view::npos) return std::nullopt;
    ParsedUrl out;
    const std::string_view scheme = url.substr(0, separator);
    if (iequals_ascii(scheme, "https")) out.scheme = UrlScheme::Https;
    else if (iequals_ascii(scheme, "http")) out.scheme = UrlScheme::Http;
    else return std::nullopt;

    std::string_view authority = url.substr(separator + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    if (authority.empty() || authority.find('@') != std::string_view::npos) return std::nullopt;

    std::string_view host;
    std::string_view port_text;
    if (authority.front() == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        host = authority.substr(1, close - 1);
        const std::string_view rest = authority.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return std::nullopt;
            port_text = rest.substr(1);
        }
        const std::optional<IpAddress> address = IpAddress::parse(host);
        if (!address || address->is_v4()) return std::nullopt;
    } else {
        const std::size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string_view::npos) port_text = authority.substr(colon + 1);
        if (host.empty() || !std::ranges::all_of(host, is_name_char)) return std::nullopt;
    }
    if (authority.ends_with(':')) return std::nullopt;
    if (!port_text.empty()) {
        const auto port = parse_port(port_text);
        if (!port) return std::nullopt;
        out.port = *port;
    }
    out.host = lowercase(host);
    return out;
}

std::string host_for_diagnostic(std::string_view url) {
    if (const std::optional<ParsedUrl> parsed = parse_url(url)) return parsed->host;
    return std::string(url);
}

std::string normalize_host(std::string_view host) {
    if (host.starts_with('[')) {
        const std::size_t close = host.find(']');
        return lowercase(host.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1));
    }
    // A single colon separates a port; more than one is a bare IPv6 literal.
    if (const std::size_t colon = host.find(':'); colon != std::string_view::npos && host.find(':', colon + 1) == std::string_view::npos)
        host = host.substr(0, colon);
    return lowercase(host);
}

}  // namespace reboot::net
