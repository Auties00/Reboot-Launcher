#include "reboot/browser/connection_state.hpp"

namespace reboot::browser {

namespace {

// Any HTTP answer, even an error status, proves the path to that host works.
[[nodiscard]] constexpr bool answered(HttpsProbe probe) noexcept {
    return probe == HttpsProbe::Ok || probe == HttpsProbe::Unavailable;
}

// The edge host is out of reach: the internet working means the edge is down, nothing working
// means this computer is offline.
[[nodiscard]] constexpr ConnectionState by_connectivity(HttpsProbe connectivity) noexcept {
    if (answered(connectivity)) return ConnectionState::ServiceDown;
    if (connectivity == HttpsProbe::Failed) return ConnectionState::Offline;
    return ConnectionState::Backoff;
}

}  // namespace

ConnectionState classify_connect_failure(const ConnectFailureEvidence& evidence) noexcept {
    if (evidence.dns_failed) return by_connectivity(evidence.connectivity);
    switch (evidence.status) {
        case HttpsProbe::Ok:
            return evidence.quic_timed_out ? ConnectionState::UdpBlocked : ConnectionState::Backoff;
        case HttpsProbe::Unavailable: return ConnectionState::ServiceDown;
        case HttpsProbe::Failed: return by_connectivity(evidence.connectivity);
        case HttpsProbe::NotRun: break;
    }
    return ConnectionState::Backoff;
}

}  // namespace reboot::browser
