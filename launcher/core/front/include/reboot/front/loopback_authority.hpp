#pragma once

#include <optional>
#include <string_view>

#include "reboot/foundation/net_types.hpp"

namespace rb::front {

// The Host and Origin a loopback listener accepts, against DNS rebinding and browser pages.
struct LoopbackAuthority {
    Port port;

    // 127.0.0.1 or localhost, with this port or none: UE's WebSocket client sends no port.
    [[nodiscard]] bool allows_host(std::string_view host_header) const;

    // Absent, http://<loopback>:<port>, or the bare loopback authority UE's WebSocket client sends.
    [[nodiscard]] bool allows_origin(std::optional<std::string_view> origin_header) const;
};

}  // namespace rb::front
