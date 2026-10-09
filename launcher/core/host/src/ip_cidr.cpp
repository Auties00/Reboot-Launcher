#include "reboot/host/ip_cidr.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>

#include "reboot/host/host_error.hpp"

namespace rb::host {

namespace {

// IPv4 is stored IPv4-mapped, so its prefix starts after the 96 bits of ::ffff:0:0/96.
[[nodiscard]] std::size_t significant_bits(const IpCidr& cidr) noexcept {
    const std::size_t prefix = std::min<std::size_t>(cidr.prefix, cidr.max_prefix());
    return cidr.address.is_v4() ? 96 + prefix : prefix;
}

// The mask that keeps the top `bits` (1 to 8) of a byte.
[[nodiscard]] u8 byte_mask(std::size_t bits) noexcept {
    return bits >= 8 ? u8{0xFF} : static_cast<u8>(0xFFu << (8 - bits));
}

}  // namespace

Result<IpCidr> IpCidr::parse(std::string_view text) {
    const auto invalid = [&] {
        return std::unexpected(
            to_diagnostic(HostError{.code = HostErrorCode::InvalidOperatorAddress, .address = std::string(text)}));
    };
    const std::size_t slash = text.find('/');
    const auto address = IpAddress::parse(text.substr(0, slash));
    if (!address) return invalid();

    IpCidr cidr{*address, 0};
    cidr.prefix = cidr.max_prefix();
    if (slash != std::string_view::npos) {
        const std::string_view digits = text.substr(slash + 1);
        if (digits.empty() || digits.size() > 3) return invalid();
        unsigned prefix = 0;
        const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), prefix);
        if (ec != std::errc{} || end != digits.data() + digits.size() || prefix > cidr.max_prefix())
            return invalid();
        cidr.prefix = static_cast<u8>(prefix);
    }
    return cidr.canonical();
}

IpCidr IpCidr::canonical() const noexcept {
    IpCidr out{address, std::min(prefix, max_prefix())};
    const std::size_t bits = significant_bits(out);
    for (std::size_t i = 0; i < out.address.bytes.size(); ++i) {
        const std::size_t start = i * 8;
        if (start >= bits) {
            out.address.bytes[i] = 0;
        } else {
            out.address.bytes[i] &= byte_mask(bits - start);
        }
    }
    return out;
}

std::string IpCidr::to_string() const {
    std::string text = address.to_string();
    if (prefix != max_prefix()) text += "/" + std::to_string(prefix);
    return text;
}

bool IpCidr::contains(const IpAddress& candidate) const noexcept {
    if (candidate.is_v4() != address.is_v4()) return false;
    const std::size_t bits = significant_bits(*this);
    for (std::size_t i = 0; i * 8 < bits; ++i) {
        const u8 mask = byte_mask(bits - i * 8);
        if ((address.bytes[i] & mask) != (candidate.bytes[i] & mask)) return false;
    }
    return true;
}

}  // namespace rb::host
