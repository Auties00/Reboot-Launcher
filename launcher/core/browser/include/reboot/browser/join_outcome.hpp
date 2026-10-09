#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "reboot/browser/server_row.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/foundation/version.hpp"

namespace rb::browser {

// A granted join. Play records `endpoint` for the session and returns it from the backend's
// ResolveMatchTarget; the port is the grant's, never an assumed 7777.
struct JoinOutcome {
    ServerDetails server;
    // Always IPv4: the game and our hosts speak only IPv4.
    Endpoint endpoint;
    // Reserved by rbsb/1 for game servers that admit only browser-routed players; kept unused.
    std::vector<u8> ticket;
    std::chrono::system_clock::time_point ticket_expires_at;
};

// Refused is the user declining ConfirmJoin; a password prompt is declined by cancelling the op.
enum class JoinFailureCode : u8 {
    OwnServer,
    NotFound,
    Offline,
    VersionMismatch,
    Unreachable,
    WrongPassword,
    TooManyAttempts,
    UnsupportedAddressFamily,
    Refused,
    EdgeUnavailable,
};

struct JoinFailure {
    JoinFailureCode code = JoinFailureCode::EdgeUnavailable;
    ServerId server;
    std::chrono::milliseconds retry_after{0};
    // VersionMismatch: what the server runs and the build that would have been launched.
    std::string server_version;
    std::optional<GameVersion> local_version;
    // UnsupportedAddressFamily: the IPv6 endpoint the edge granted.
    std::optional<Endpoint> granted;
    // The connection or request error behind EdgeUnavailable.
    std::optional<Diagnostic> cause;
};

[[nodiscard]] Diagnostic to_diagnostic(const JoinFailure& failure);

}  // namespace rb::browser
