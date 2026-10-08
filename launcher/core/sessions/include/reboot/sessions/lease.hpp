#pragma once

#include <optional>

#include "reboot/foundation/operation.hpp"

namespace reboot::sessions {

// Engine: the session survives every UI. Client: it stops when that connection closes.
struct Lease {
    // Unset for the Engine lease.
    std::optional<ConnectionId> client;

    [[nodiscard]] static Lease engine() noexcept { return {}; }
    [[nodiscard]] static Lease client_of(ConnectionId connection) noexcept { return {connection}; }
    [[nodiscard]] bool held_by_engine() const noexcept { return !client; }

    bool operator==(const Lease&) const = default;
};

}  // namespace reboot::sessions
