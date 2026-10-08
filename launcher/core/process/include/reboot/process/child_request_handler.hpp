#pragma once

#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/process/child_reply.hpp"

namespace reboot::process {

// Covers no capability ids; answers child-to-engine requests (backend ResolveMatchTarget).
class ChildRequestHandler {
public:
    virtual ~ChildRequestHandler() = default;

    // The frame types this handler answers; every other post-handshake frame is an event.
    [[nodiscard]] virtual bool handles(u64 frame_type) const = 0;
    // Strand-only. The payload is valid during the call only; `reply` may be answered later.
    virtual void on_request(const RawFrame& frame, ChildReply reply) = 0;
};

}  // namespace reboot::process
