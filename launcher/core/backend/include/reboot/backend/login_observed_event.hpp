#pragma once

#include <string>

#include "reboot/foundation/types.hpp"

namespace rb::backend {

// Not an API event: the embedded backend saw the session log in, a fallback LoggedIn for
// sessions without our client DLL. BackendProcess maps the reported session key back to the
// session and drops a key it does not know.
struct LoginObservedEvent {
    SessionId session;
    std::string account_id;
};

}  // namespace rb::backend
