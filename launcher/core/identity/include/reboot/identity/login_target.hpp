#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/types.hpp"
#include "reboot/identity/backend_login.hpp"
#include "reboot/storage/backend_target.hpp"

namespace rb::identity {

// Reboot answers GET /reboot/v1/backend-info; anything else is ThirdParty.
enum class UpstreamFlavor : u8 { Reboot, ThirdParty };

// IdentityService::login_target builds it from the backend target and that endpoint's BackendLogin.
struct LoginTarget {
    storage::BackendKind backend = storage::BackendKind::Embedded;
    // Ignored for Embedded.
    UpstreamFlavor flavor = UpstreamFlavor::Reboot;
    // BackendLogin::login; its password is the endpoint's RemoteBackendPassword.
    std::optional<std::string> remote_login;
    CredentialPolicy policy = CredentialPolicy::Ticket;
    bool custom_auth_dll = false;

    bool operator==(const LoginTarget&) const = default;
};

}  // namespace rb::identity
