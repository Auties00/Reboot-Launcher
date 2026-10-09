#include "reboot/foundation/net_types.hpp"

#include <algorithm>
#include <cstddef>

namespace rb {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Strict dotted quad: four decimal parts, no leading zeros, each at most 255.
std::optional<std::array<u8, 4>> parse_v4(std::string_view text) {
    std::array<u8, 4> out{};
    std::size_t part = 0;
    std::size_t i = 0;
    while (true) {
        const std::size_t start = i;
        unsigned value = 0;
        while (i < text.size() && is_digit(text[i]) && i - start < 3) value = value * 10 + static_cast<unsigned>(text[i++] - '0');
        const std::size_t length = i - start;
        if (length == 0 || value > 255 || (length > 1 && text[start] == '0')) return std::nullopt;
        out[part++] = static_cast<u8>(value);
        if (part == 4) break;
        if (i >= text.size() || text[i] != '.') return std::nullopt;
        ++i;
    }
    if (i != text.size()) return std::nullopt;
    return out;
}

std::optional<std::array<u8, 16>> parse_v6(std::string_view text) {
    std::array<u16, 8> groups{};
    std::size_t count = 0;
    std::optional<std::size_t> gap;
    std::size_t i = 0;
    if (text.starts_with("::")) {
        gap = 0;
        i = 2;
    } else if (text.starts_with(':')) {
        return std::nullopt;
    }
    while (i < text.size()) {
        if (count == 8) return std::nullopt;
        // An embedded IPv4 address ends the text and fills the last two groups.
        if (text.find('.', i) != std::string_view::npos && text.find(':', i) == std::string_view::npos) {
            const auto quad = parse_v4(text.substr(i));
            if (!quad || count > 6) return std::nullopt;
            groups[count++] = static_cast<u16>((*quad)[0] << 8 | (*quad)[1]);
            groups[count++] = static_cast<u16>((*quad)[2] << 8 | (*quad)[3]);
            i = text.size();
            break;
        }
        const std::size_t start = i;
        unsigned value = 0;
        while (i < text.size() && hex_value(text[i]) >= 0 && i - start < 4)
            value = value << 4 | static_cast<unsigned>(hex_value(text[i++]));
        if (i == start) return std::nullopt;
        groups[count++] = static_cast<u16>(value);
        if (i == text.size()) break;
        if (text[i] != ':') return std::nullopt;
        ++i;
        if (i < text.size() && text[i] == ':') {
            if (gap) return std::nullopt;
            gap = count;
            ++i;
        } else if (i == text.size()) {
            return std::nullopt;
        }
    }
    if (gap ? count > 7 : count != 8) return std::nullopt;

    std::array<u16, 8> full{};
    if (gap) {
        std::copy_n(groups.begin(), *gap, full.begin());
        std::copy(groups.begin() + static_cast<std::ptrdiff_t>(*gap), groups.begin() + static_cast<std::ptrdiff_t>(count),
                  full.end() - static_cast<std::ptrdiff_t>(count - *gap));
    } else {
        full = groups;
    }
    std::array<u8, 16> out{};
    for (std::size_t g = 0; g < 8; ++g) {
        out[2 * g] = static_cast<u8>(full[g] >> 8);
        out[2 * g + 1] = static_cast<u8>(full[g]);
    }
    return out;
}

}  // namespace

std::expected<Port, PortError> parse_port(std::string_view text) {
    if (text.empty()) return std::unexpected(PortError::Empty);
    if (!std::ranges::all_of(text, is_digit)) return std::unexpected(PortError::NotNumeric);
    const std::size_t first = text.find_first_not_of('0');
    if (first == std::string_view::npos || text.size() - first > 5) return std::unexpected(PortError::OutOfRange);
    unsigned value = 0;
    for (const char c : text.substr(first)) value = value * 10 + static_cast<unsigned>(c - '0');
    if (value > 65535) return std::unexpected(PortError::OutOfRange);
    return Port{static_cast<u16>(value)};
}

std::optional<IpAddress> IpAddress::parse(std::string_view text) {
    if (text.find(':') == std::string_view::npos) {
        const auto quad = parse_v4(text);
        if (!quad) return std::nullopt;
        return v4(u32{(*quad)[0]} << 24 | u32{(*quad)[1]} << 16 | u32{(*quad)[2]} << 8 | u32{(*quad)[3]});
    }
    const auto v6 = parse_v6(text);
    if (!v6) return std::nullopt;
    IpAddress address;
    address.bytes = *v6;
    return address;
}

// RFC 5952: lowercase, no leading zeros, the longest run of two or more zero groups as "::".
std::string IpAddress::to_string() const {
    if (is_v4()) return std::to_string(bytes[12]) + '.' + std::to_string(bytes[13]) + '.' + std::to_string(bytes[14]) +
                        '.' + std::to_string(bytes[15]);

    std::array<unsigned, 8> groups{};
    for (std::size_t g = 0; g < 8; ++g) groups[g] = unsigned{bytes[2 * g]} << 8 | bytes[2 * g + 1];
    std::size_t best_start = 8;
    std::size_t best_length = 1;
    for (std::size_t g = 0; g < 8;) {
        if (groups[g] != 0) {
            ++g;
            continue;
        }
        std::size_t end = g;
        while (end < 8 && groups[end] == 0) ++end;
        if (end - g > best_length) {
            best_start = g;
            best_length = end - g;
        }
        g = end;
    }

    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t g = 0; g < 8; ++g) {
        if (g == best_start) {
            out += "::";
            g += best_length - 1;
            continue;
        }
        if (!out.empty() && out.back() != ':') out.push_back(':');
        bool started = false;
        for (int shift = 12; shift >= 0; shift -= 4) {
            const unsigned digit = (groups[g] >> shift) & 0xF;
            if (digit != 0 || started || shift == 0) {
                out.push_back(kDigits[digit]);
                started = true;
            }
        }
    }
    return out;
}

std::expected<HostPort, AddressError> parse_host_port(std::string_view text) {
    if (text.empty()) return std::unexpected(AddressError::Empty);

    if (text.front() == '[') {
        const std::size_t close = text.find(']');
        if (close == std::string_view::npos) return std::unexpected(AddressError::BracketMismatch);
        const std::string_view host = text.substr(1, close - 1);
        if (host.find(':') == std::string_view::npos || !IpAddress::parse(host))
            return std::unexpected(AddressError::BadHost);
        const std::string_view rest = text.substr(close + 1);
        if (rest.empty()) return HostPort{std::string(host), std::nullopt};
        if (rest.front() != ':') return std::unexpected(AddressError::BracketMismatch);
        const auto port = parse_port(rest.substr(1));
        if (!port) return std::unexpected(AddressError::BadPort);
        return HostPort{std::string(host), *port};
    }
    if (text.find(']') != std::string_view::npos) return std::unexpected(AddressError::BracketMismatch);

    const std::size_t colon = text.find(':');
    if (colon != text.rfind(':')) {
        if (!IpAddress::parse(text)) return std::unexpected(AddressError::BadHost);
        return HostPort{std::string(text), std::nullopt};
    }
    const std::string_view host = text.substr(0, colon);
    const bool host_ok = !host.empty() && std::ranges::all_of(host, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '-' || c == '.' || c == '_';
    });
    if (!host_ok) return std::unexpected(AddressError::BadHost);
    if (colon == std::string_view::npos) return HostPort{std::string(host), std::nullopt};
    const auto port = parse_port(text.substr(colon + 1));
    if (!port) return std::unexpected(AddressError::BadPort);
    return HostPort{std::string(host), *port};
}

std::string Endpoint::to_string() const {
    const std::string host = address.to_string();
    const std::string port_text = std::to_string(port.value);
    return address.is_v4() ? host + ':' + port_text : '[' + host + "]:" + port_text;
}

}  // namespace rb
