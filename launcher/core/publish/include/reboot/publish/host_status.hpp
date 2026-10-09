#pragma once

#include "reboot/foundation/types.hpp"

namespace rb::publish {

// The edge's verdict on a registered entry. AwaitingProbe: no probe reply yet, so the edge keeps the
// entry hidden. LiveUnreachable: probes are failing; the edge hides the entry after 3 failures.
enum class HostStatus : u8 { AwaitingProbe, Live, LiveUnreachable };

// From rbsb/1 HostStatus{reachable, probe_failures}.
[[nodiscard]] constexpr HostStatus host_status(bool reachable, u32 probe_failures) noexcept {
    if (reachable) return HostStatus::Live;
    return probe_failures == 0 ? HostStatus::AwaitingProbe : HostStatus::LiveUnreachable;
}

}  // namespace rb::publish
