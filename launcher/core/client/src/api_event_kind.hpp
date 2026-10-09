#pragma once

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/types.hpp"

namespace rb::client {

// The reboot.api.v1 EventKind values the library reads or raises; it links no API code.
enum class ApiEventKind : u32 {
    Resync = 1,
    ConnectionLost = 2,
    Reconnected = 3,
    OpCompleted = 5,
};

// A payload-free event the library raises itself.
[[nodiscard]] inline contracts::ipc::WireEvent library_event(ApiEventKind kind, u64 epoch) {
    contracts::ipc::WireEvent event;
    event.kind = static_cast<u32>(kind);
    event.epoch = epoch;
    return event;
}

}  // namespace rb::client
