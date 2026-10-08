#pragma once

#include <chrono>
#include <string>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/version.hpp"

namespace reboot::backend {

// The session is the lease's: a credential is minted only for a configured session.
struct LaunchCredentialRequest {
    std::string account_id;
    contracts::backend::CredentialKind kind = contracts::backend::CredentialKind::ExchangeCode;
    Changelist build;
};

// The -AUTH_PASSWORD value: single use, valid 5 minutes and only while its session's
// ConfigureSession is live, bound to the account and the build. The token it redeems for dies
// with the session. Registered with the Redactor on receipt.
struct LaunchCredential {
    SecretString value;
    std::chrono::system_clock::time_point expires_at;
};

}  // namespace reboot::backend
