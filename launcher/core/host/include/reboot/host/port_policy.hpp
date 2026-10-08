#pragma once

#include <variant>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::host {

// Inclusive; the whole block has to fit inside it.
struct PortRange {
    Port first;
    Port last;

    constexpr auto operator<=>(const PortRange&) const = default;
};

// Five {game, beacon} pairs, and four blocks fit within the rbsb per-IP cap of 4 hosts.
inline constexpr PortRange kDefaultAutoRange{Port{7777}, Port{7786}};
inline constexpr Port kDefaultPinnedPort{7777};
inline constexpr Port kMinHostPort{1024};
// LegacyFixed's backend port; compared as a number, so "03551" cannot slip past.
inline constexpr Port kReservedBackendPort{3551};

// The block is {first, first + 1, ...}; a taken port fails the start, never moves it.
struct PinnedPorts {
    Port first = kDefaultPinnedPort;

    constexpr auto operator<=>(const PinnedPorts&) const = default;
};

// The lowest free block inside the range; blocks that cover kReservedBackendPort are skipped.
struct AutoPorts {
    PortRange range = kDefaultAutoRange;

    constexpr auto operator<=>(const AutoPorts&) const = default;
};

using PortPolicy = std::variant<PinnedPorts, AutoPorts>;

// host.invalid_port_policy for a port below kMinHostPort or an inverted range;
// host.reserved_port for a pinned block starting at kReservedBackendPort. The block size is
// known only at start: a pinned block that covers the reserved port fails there.
[[nodiscard]] Result<void> validate(const PortPolicy& policy);

}  // namespace reboot::host
