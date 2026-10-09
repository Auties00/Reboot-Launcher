#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <optional>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"

namespace rb {
class Executor;
class IClock;
class TimerService;
}  // namespace rb

namespace rb::net {

class IDatagramConnector;

// An attempt resends at each offset in `resend_at` until a reply, a refusal or `attempt_timeout`;
// a lost datagram costs a resend, not a whole attempt.
struct ProbePolicy {
    u32 attempts = 3;
    std::chrono::milliseconds attempt_timeout = std::chrono::seconds{2};
    std::chrono::milliseconds interval = std::chrono::seconds{1};
    std::array<std::chrono::milliseconds, 3> resend_at{std::chrono::milliseconds{0}, std::chrono::milliseconds{300},
                                                       std::chrono::milliseconds{1000}};
};

// Refused is an ICMP port unreachable reported on the connected socket.
enum class ProbeOutcome : u8 { Alive, Refused, TimedOut, Cancelled };

struct ProbeResult {
    ProbeOutcome outcome = ProbeOutcome::TimedOut;
    Endpoint target;
    u32 attempts_used = 0;
    std::optional<std::chrono::milliseconds> rtt;
};

// Capabilities: matchmaking-networking.udp-ping.
// Sends contracts::game_server::kRbsbProbe, the 25-byte probe every game port answers, over a
// connected datagram channel, so only replies from the target count; any reply is Alive.
// Strand-only: the resend schedule runs on TimerService and the RTT comes from IClock.
class UdpBeaconProber {
public:
    UdpBeaconProber(IDatagramConnector& connector, Executor& strand, TimerService& timers, IClock& clock);
    ~UdpBeaconProber();
    UdpBeaconProber(const UdpBeaconProber&) = delete;
    UdpBeaconProber& operator=(const UdpBeaconProber&) = delete;

    // Fails synchronously on port 0, zero attempts or a zero timeout. `done` runs on the strand
    // exactly once; cancelling yields Cancelled.
    Result<void> probe(Endpoint target, ProbePolicy policy, CancelToken token, UniqueFunction<void(ProbeResult)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::net
