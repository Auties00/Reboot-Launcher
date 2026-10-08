#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/net/port_mapping.hpp"

namespace reboot::net {

// One entry of the sweep marker kept in state/state.json. It lets the next engine start delete a
// mapping a crash left behind, including NAT-PMP entries, which carry no description.
struct MappingRecord {
    SessionId session;
    PortMapping mapping;
};

}  // namespace reboot::net
