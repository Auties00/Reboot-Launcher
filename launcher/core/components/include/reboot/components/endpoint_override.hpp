#pragma once

#include <string>

#include "reboot/foundation/net_types.hpp"

namespace reboot::components {

// The rbsb endpoint named by the signed manifest, read in place of the compiled-in
// sb.rebootfn.org:443 even when no update is offered.
struct EndpointOverride {
    std::string host;
    Port port;

    bool operator==(const EndpointOverride&) const = default;
};

}  // namespace reboot::components
