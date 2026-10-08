#pragma once

#include <optional>
#include <string>

#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::identity {

// Ticket: the game gets a session ticket that the front swaps. LegacyArgv: an opt-in, with a
// warning, that puts the real password in argv for a custom auth DLL talking to a hosted backend.
enum class CredentialPolicy : u8 { Ticket, LegacyArgv };

// How the client logs in to one Local or Remote backend; only play logs in, so it has no role.
// Keyed like the RemoteBackendPassword secret that holds its password: by the endpoint as
// BackendTarget::normalize leaves it.
struct BackendLogin {
    HostPort endpoint;
    // Login of a password-backed account; unset logs in as the account id.
    std::optional<std::string> login;
    CredentialPolicy policy = CredentialPolicy::Ticket;

    bool operator==(const BackendLogin&) const = default;
};

}  // namespace reboot::identity
