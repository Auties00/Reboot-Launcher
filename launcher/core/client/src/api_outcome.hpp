#pragma once

#include <optional>
#include <vector>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::client {

// reboot.api.v1 Outcome up to failed = 4, enough to fail an op the engine can no longer finish.
struct ApiOutcome {
    u64 op_id = 0;
    u32 method_id = 0;
    std::optional<std::vector<u8>> completed;
    std::optional<contracts::common::WireDiagnostic> failed;
};

[[nodiscard]] std::vector<u8> encode_failed_outcome(u64 op_id, u32 method_id, const Diagnostic& reason);

}  // namespace reboot::client
