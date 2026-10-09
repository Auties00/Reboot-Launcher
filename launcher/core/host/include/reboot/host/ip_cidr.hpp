#pragma once

#include <string>
#include <string_view>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::host {

// `prefix` counts in the address's own family: at most 32 for IPv4, 128 for IPv6.
struct IpCidr {
    IpAddress address;
    u8 prefix = 0;

    // "a.b.c.d", "a.b.c.d/n", "v6" or "v6/n"; a bare address is a /32 or /128. Host bits are
    // cleared. host.invalid_operator_address names the text otherwise.
    [[nodiscard]] static Result<IpCidr> parse(std::string_view text);

    [[nodiscard]] constexpr u8 max_prefix() const noexcept { return address.is_v4() ? 32 : 128; }
    // Host bits cleared, so equal blocks compare and print the same; requires a valid prefix.
    [[nodiscard]] IpCidr canonical() const noexcept;
    // A full-length prefix prints as the bare address.
    [[nodiscard]] std::string to_string() const;
    // Never across families: an IPv4 block does not match its IPv4-mapped IPv6 spelling.
    [[nodiscard]] bool contains(const IpAddress& candidate) const noexcept;

    constexpr auto operator<=>(const IpCidr&) const = default;
};

}  // namespace rb::host
