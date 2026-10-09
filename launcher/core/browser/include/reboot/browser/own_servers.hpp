#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::browser {

// The server ids this user publishes; the engine wires it to the host identities. Lists leave
// them out and joins refuse them.
class IOwnServers {
public:
    virtual ~IOwnServers() = default;
    [[nodiscard]] virtual bool owns(const ServerId& id) const = 0;
};

}  // namespace rb::browser
