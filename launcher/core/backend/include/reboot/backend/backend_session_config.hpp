#pragma once

#include <string>

#include "reboot/backend/console_key.hpp"
#include "reboot/foundation/secret.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::backend {

// ConfigureSession: what the embedded backend needs to serve one play or host session.
struct BackendSessionConfig {
    // The front's /s/<session_key>/ key, as 32 hex digits; the front registers it with the Redactor.
    SecretString session_key;
    std::string account_id;
    // http://127.0.0.1:<front port>/s/<session_key>/, written into the hotfix ServerAddr and the
    // ticket serviceUrl; secret because it embeds the key.
    SecretString origin;
    ConsoleKey console_key;
    GameVersion version;
    Changelist changelist;
};

}  // namespace rb::backend
