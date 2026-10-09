#pragma once

#include "reboot/browser/own_servers.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::publish {
class HostIdentityStore;
}

namespace rb::host {
class HostProfileStore;
}

namespace rb::engine {

// Capabilities: none; keeps the browser from listing or joining this user's own servers.
// Strand-only. A server id is ours when a host profile's identity carries it.
class HostIdentityOwnServers final : public browser::IOwnServers {
public:
    HostIdentityOwnServers(const publish::HostIdentityStore& identities, const host::HostProfileStore& profiles) noexcept
        : identities_(identities), profiles_(profiles) {}

    [[nodiscard]] bool owns(const ServerId& id) const override;

private:
    const publish::HostIdentityStore& identities_;
    const host::HostProfileStore& profiles_;
};

}  // namespace rb::engine
