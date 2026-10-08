#include "authority.hpp"

#include <algorithm>

namespace reboot::front {

namespace {

[[nodiscard]] bool is_label_char(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

[[nodiscard]] bool is_dns_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 253) return false;
    std::string_view last;
    while (!name.empty()) {
        const std::size_t dot = name.find('.');
        const std::string_view label = name.substr(0, dot);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        if (!std::ranges::all_of(label, is_label_char)) return false;
        last = label;
        name = dot == std::string_view::npos ? std::string_view{} : name.substr(dot + 1);
        if (dot != std::string_view::npos && name.empty()) return false;
    }
    // A numeric last label is an IP form the resolver would accept (127.1, 0x7f000001), not a name.
    return !(last.front() >= '0' && last.front() <= '9');
}

}  // namespace

std::optional<Authority> split_authority(std::string_view text) {
    Authority out;
    std::string_view port_text;
    bool has_port = false;
    if (text.starts_with('[')) {
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        out.host = text.substr(1, close - 1);
        const std::string_view rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return std::nullopt;
            port_text = rest.substr(1);
            has_port = true;
        }
        if (out.host.find(':') == std::string_view::npos) return std::nullopt;
    } else {
        const std::size_t colon = text.find(':');
        if (colon != std::string_view::npos && text.find(':', colon + 1) != std::string_view::npos) return std::nullopt;
        out.host = text.substr(0, colon);
        if (colon != std::string_view::npos) {
            port_text = text.substr(colon + 1);
            has_port = true;
        }
    }
    if (out.host.empty()) return std::nullopt;
    if (has_port) {
        const auto port = parse_port(port_text);
        if (!port) return std::nullopt;
        out.port = *port;
    }
    return out;
}

std::optional<std::string> normalize_host(std::string_view host) {
    std::string lower(host);
    std::ranges::transform(lower, lower.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    if (lower.find(':') != std::string::npos) {
        const auto address = IpAddress::parse(lower);
        if (!address) return std::nullopt;
        return address->to_string();
    }
    if (lower.ends_with('.')) lower.pop_back();
    if (const auto address = IpAddress::parse(lower)) return address->to_string();
    if (!is_dns_name(lower)) return std::nullopt;
    return lower;
}

std::optional<UpstreamOrigin> origin_from_authority(net::UrlScheme scheme, std::string_view authority) {
    const auto split = split_authority(authority);
    if (!split) return std::nullopt;
    auto host = normalize_host(split->host);
    if (!host) return std::nullopt;
    return UpstreamOrigin{scheme, std::move(*host), split->port.value_or(default_port(scheme))};
}

bool reaches_this_machine(const IpAddress& address) noexcept {
    if (address.is_loopback()) return true;
    if (address.is_v4()) return address.bytes[12] == 0 && address.bytes[13] == 0 && address.bytes[14] == 0 &&
                                address.bytes[15] == 0;
    return std::ranges::all_of(address.bytes, [](u8 b) { return b == 0; });
}

bool is_loopback_host(std::string_view normalized_host) {
    if (normalized_host == "localhost" || normalized_host.ends_with(".localhost")) return true;
    const auto address = IpAddress::parse(normalized_host);
    return address && reaches_this_machine(*address);
}

}  // namespace reboot::front
