#include "host_identity_own_servers.hpp"

#include <optional>

#include "reboot/host/host_profile_store.hpp"
#include "reboot/publish/host_identity_store.hpp"

namespace reboot::engine {

bool HostIdentityOwnServers::owns(const ServerId& id) const {
    for (const host::HostProfile& profile : profiles_.list())
        if (const std::optional<ServerId> server = identities_.server_id(profile.id); server && *server == id) return true;
    return false;
}

}  // namespace reboot::engine
