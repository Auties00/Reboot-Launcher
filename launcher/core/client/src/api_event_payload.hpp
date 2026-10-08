#pragma once

#include <optional>
#include <span>
#include <vector>

#include "reboot/contracts/ipc.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::client {

// reboot.api.v1 EventPayload up to op_completed = 2, each message kept encoded.
struct ApiEventPayload {
    std::optional<std::vector<u8>> op_progress;
    std::optional<std::vector<u8>> op_completed;
};

// The OpCompleted event the library delivers from an op's encoded Outcome.
[[nodiscard]] contracts::ipc::WireEvent op_completed_event(u64 epoch, u64 op_id, std::span<const u8> outcome);

}  // namespace reboot::client
