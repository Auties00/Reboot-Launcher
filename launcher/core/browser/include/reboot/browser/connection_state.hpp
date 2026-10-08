#pragma once

#include <cstddef>
#include <optional>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/executor.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::browser {

// UdpBlocked, Offline and ServiceDown say why the last attempt failed; the session keeps retrying
// on the backoff while a lease is held.
enum class ConnectionState : u8 {
    Idle,
    Connecting,
    Connected,
    Backoff,
    // GoAway received: the connection still serves until the edge closes it or the delay ends.
    Draining,
    UdpBlocked,
    Offline,
    ServiceDown,
};

// Payload of EventKind::BrowserConnectionChanged.
struct ConnectionStatus {
    ConnectionState state = ConnectionState::Idle;
    std::optional<SteadyTime> next_attempt;
    std::optional<Diagnostic> error;

    [[nodiscard]] std::size_t approx_bytes() const noexcept { return sizeof(ConnectionStatus); }
};

// NotRun: an earlier check already decided the state.
enum class HttpsProbe : u8 { NotRun, Ok, Unavailable, Failed };

// What one failed connect attempt saw. `status` is GET /status on the edge host; `connectivity`
// is a known HTTPS URL on another host.
struct ConnectFailureEvidence {
    bool dns_failed = false;
    bool quic_timed_out = false;
    HttpsProbe status = HttpsProbe::NotRun;
    HttpsProbe connectivity = HttpsProbe::NotRun;
};

// Capabilities: server-browser.+25.
// NXDOMAIN with working HTTPS is ServiceDown; a QUIC timeout while /status answers is UdpBlocked;
// anything the probes cannot explain is Backoff.
[[nodiscard]] ConnectionState classify_connect_failure(const ConnectFailureEvidence& evidence) noexcept;

}  // namespace reboot::browser
