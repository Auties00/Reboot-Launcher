#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "reboot/contracts/backend.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/native_path.hpp"
#include "reboot/testing/child_misbehaviour.hpp"

namespace rb::testing {

// What FakeBackend does. The defaults describe a conforming reboot-backend.
struct FakeBackendScript {
    ChildMisbehaviour child;
    // Between Welcome and Ready.
    std::chrono::milliseconds ready_delay{0};
    // Exits with kBindFailureExitCode after Welcome instead of sending Ready.
    bool bind_failure = false;
    // Binds the Welcome address on an OS-assigned port, reports it in Ready and answers
    // GET /reboot/v1/backend-info. Off, Ready names a port nothing listens on.
    bool serve_http = false;
    contracts::backend::ContentVersion content{1, 1};
    // Sent to the engine after Ready, to drive the pulled match target.
    std::vector<contracts::backend::ResolveMatchTarget> match_target_requests;
    // Sent after Ready, as the fallback LoggedIn signal.
    std::vector<contracts::backend::LoginObserved> logins;
};

// Every fake script's JSON mirrors its struct, so to_json round-trips: keys are field names,
// durations take an _ms suffix, byte arrays are hex, enums and value types are their text forms, and
// a variant is {"<alternative, snake_case, less any Script prefix>": {fields}}, bare without fields.
// Unknown keys fail with testing.bad_script, so a typo never passes as a conforming default.
[[nodiscard]] Result<FakeBackendScript> parse_fake_backend_script(std::string_view json);
[[nodiscard]] Result<FakeBackendScript> load_fake_backend_script(const NativePath& file);
[[nodiscard]] std::string to_json(const FakeBackendScript& script);

}  // namespace rb::testing
