#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "reboot/base/types.hpp"
#include "reboot/foundation/operation.hpp"

namespace rb::engine {

// An op a client started through the API: Engine.operations lists it while it is live, and it is
// the EventKind::OpStarted payload.
struct StartedOp {
    OpId op;
    u32 method_id = 0;
    // The encoded request; it holds no secret, since secrets only travel in SecretPut.
    std::vector<u8> request;
    DisconnectPolicy disconnect{};
    std::optional<SessionId> session;
    std::chrono::system_clock::time_point started_at;
};

}  // namespace rb::engine
