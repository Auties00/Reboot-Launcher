#pragma once

#include "reboot/foundation/types.hpp"
#include "reboot/ports/ipc.hpp"

namespace rb::os_macos::ipc {

// What MacCallerContext::detect reads from SessionGetInfo and the process.
struct CallerFacts {
    u32 audit_session_id = 0;
    // sessionHasGraphicAccess is set.
    bool graphic_access = false;
    u32 euid = 0;
};

// The rules MacCallerContext documents, over facts already read.
[[nodiscard]] ports::CallerContext caller_context_from(const CallerFacts& facts);

}  // namespace rb::os_macos::ipc
