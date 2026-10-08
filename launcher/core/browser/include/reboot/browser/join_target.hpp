#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <variant>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::browser {

// A browser server picked from the list or a confirmed link. Name and author are what the user
// confirmed; a launch always asks the edge again for a fresh JoinGrant.
struct ServerTarget {
    ServerId id;
    std::string name;
    std::string author;

    bool operator==(const ServerTarget&) const = default;
};

// Typed by the user: no JoinGrant, no ticket and no server-side password check.
struct AddressTarget {
    // As typed, for display.
    std::string text;
    // Port 7777 when the text had none.
    HostPort address;

    bool operator==(const AddressTarget&) const = default;
};

// What Play joins when its request names nothing; absent means the local or linked auto-server.
struct JoinTarget {
    std::variant<ServerTarget, AddressTarget> target;

    bool operator==(const JoinTarget&) const = default;
};

// Payload of EventKind::JoinTargetChanged.
struct JoinTargetChanged {
    std::optional<JoinTarget> target;

    [[nodiscard]] std::size_t approx_bytes() const noexcept { return sizeof(JoinTargetChanged); }
};

}  // namespace reboot::browser
