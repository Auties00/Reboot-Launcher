#pragma once

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/process/child_exit_info.hpp"

namespace reboot::process {

// Covers no capability ids; the owner's view of a supervised child (backend or game server).
// Every call runs on the strand.
class ChildObserver {
public:
    virtual ~ChildObserver() = default;

    // Welcome was sent. `generation` counts spawns, so the owner replays its per-child state.
    virtual void on_running(u32 generation) = 0;
    // Post-handshake frames that are not replies, child requests, Ping, Pong or Log (backend Ready,
    // game-server Listening). The payload is valid during the call only.
    virtual void on_event(const RawFrame& frame) = 0;
    // `miss_limit` Pings in a row went unanswered. Without a RestartPolicy this is the only
    // reaction; with one, the child is killed and restarted.
    virtual void on_unresponsive(u32 missed) = 0;
    virtual void on_exit(const ChildExitInfo& exit) = 0;
};

}  // namespace reboot::process
