#pragma once

#include <string_view>

#include "reboot/foundation/types.hpp"

namespace reboot::net {

enum class PortProtocol : u8 { Udp, Tcp };

[[nodiscard]] constexpr std::string_view protocol_name(PortProtocol protocol) noexcept {
    return protocol == PortProtocol::Udp ? "UDP" : "TCP";
}

}  // namespace reboot::net
