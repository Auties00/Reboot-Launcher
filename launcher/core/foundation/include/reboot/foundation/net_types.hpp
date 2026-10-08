#pragma once

#include <array>
#include <compare>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot {

struct Port {
    u16 value{};

    constexpr auto operator<=>(const Port&) const = default;
};

enum class PortError : u8 { Empty, NotNumeric, OutOfRange };

// Port 0 is OutOfRange: callers that want an OS-assigned port do not parse one.
[[nodiscard]] std::expected<Port, PortError> parse_port(std::string_view text);

// IPv4 is stored IPv4-mapped, as sb::IpAddr does, so every address has one representation.
struct IpAddress {
    std::array<u8, 16> bytes{};

    constexpr auto operator<=>(const IpAddress&) const = default;

    [[nodiscard]] static constexpr IpAddress v4(u32 host_order) noexcept {
        IpAddress a;
        a.bytes[10] = 0xFF;
        a.bytes[11] = 0xFF;
        a.bytes[12] = static_cast<u8>(host_order >> 24);
        a.bytes[13] = static_cast<u8>(host_order >> 16);
        a.bytes[14] = static_cast<u8>(host_order >> 8);
        a.bytes[15] = static_cast<u8>(host_order);
        return a;
    }

    [[nodiscard]] static std::optional<IpAddress> parse(std::string_view text);
    [[nodiscard]] std::string to_string() const;

    [[nodiscard]] constexpr bool is_v4() const noexcept {
        for (std::size_t i = 0; i < 10; ++i)
            if (bytes[i] != 0) return false;
        return bytes[10] == 0xFF && bytes[11] == 0xFF;
    }

    [[nodiscard]] constexpr bool is_loopback() const noexcept {
        if (is_v4()) return bytes[12] == 127;
        for (std::size_t i = 0; i < 15; ++i)
            if (bytes[i] != 0) return false;
        return bytes[15] == 1;
    }
};

struct HostPort {
    std::string host;
    std::optional<Port> port;

    bool operator==(const HostPort&) const = default;
};

enum class AddressError : u8 { Empty, BadHost, BadPort, BracketMismatch };

// Accepts "host", "host:port" and "[v6]:port"; a bare IPv6 literal has no port.
[[nodiscard]] std::expected<HostPort, AddressError> parse_host_port(std::string_view text);

struct Endpoint {
    IpAddress address;
    Port port;

    constexpr auto operator<=>(const Endpoint&) const = default;

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] constexpr bool is_loopback() const noexcept { return address.is_loopback(); }
};

}  // namespace reboot
