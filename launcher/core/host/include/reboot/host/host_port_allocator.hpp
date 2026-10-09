#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "reboot/foundation/cancel.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/function.hpp"
#include "reboot/foundation/net_types.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/host/port_block.hpp"
#include "reboot/host/port_policy.hpp"
#include "reboot/net/our_process.hpp"

namespace rb {
class Executor;
class WorkerPool;
}  // namespace rb

namespace rb::net {
class PortPreflight;
}

namespace rb::host {

struct BlockRequest {
    SessionId session;
    PortPolicy policy;
    u16 size = 0;
    // Auto only: search above this block, after the server failed to listen on it.
    std::optional<PortBlock> after;
    // The engine's live children, so a port held by one of them is reported as ours.
    std::vector<net::OurProcess> ours;
};

// Capabilities: hosting.game-server-port.
// Strand-only. Hands out one UDP block per host session, so concurrent starts never race for
// the same ports. Every port of a candidate block is preflighted on the WorkerPool through
// PortPreflight (wildcard bind without address reuse, EADDRINUSE told apart from EACCES).
// - Pinned: exactly {P, ..., P + size - 1}; host.reserved_port when it covers
//   kReservedBackendPort, and a taken port fails with net.port_busy naming the owner, or
//   net.port_access_denied. Nothing is ever killed or moved.
// - Auto: the lowest block in the range that neither covers kReservedBackendPort nor is taken;
//   host.no_free_block when none is left. host.block_out_of_range when the block cannot fit at all.
// A session keeps its block across respawns until release().
class HostPortAllocator {
public:
    HostPortAllocator(net::PortPreflight& preflight, WorkerPool& workers, Executor& strand);
    ~HostPortAllocator();
    HostPortAllocator(const HostPortAllocator&) = delete;
    HostPortAllocator& operator=(const HostPortAllocator&) = delete;

    // Replaces a block the session already holds. Fails synchronously on size 0, an invalid policy
    // or a block that cannot exist (host.block_out_of_range, host.reserved_port). Otherwise `done`
    // runs on the strand exactly once; a cancel fails it with host.cancelled.
    Result<void> reserve(BlockRequest request, CancelToken token, UniqueFunction<void(Result<PortBlock>)> done);

    // Preflights the session's current block again before a respawn on it.
    Result<void> recheck(SessionId session, std::vector<net::OurProcess> ours, CancelToken token,
                         UniqueFunction<void(Result<PortBlock>)> done);

    void release(SessionId session);

    [[nodiscard]] std::optional<PortBlock> block(SessionId session) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rb::host
