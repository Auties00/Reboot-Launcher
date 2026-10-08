#pragma once

#include <optional>
#include <span>
#include <vector>

#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/types.hpp"

namespace reboot::client {

// Layout of reboot.api.v1 EventFilter: kinds = 1, session = 2, op_id = 3.
struct ApiEventFilter {
    std::vector<u32> kinds;
    // An encoded SessionId; the library only needs to know one is named.
    std::optional<std::vector<u8>> session;
    std::optional<u64> op_id;
};

// client.invalid_argument{name: "filter"} for bytes that are not an EventFilter.
[[nodiscard]] Result<ApiEventFilter> decode_event_filter(std::span<const u8> bytes);

// False when the filter names a session, since the library cannot tell an op's session.
[[nodiscard]] bool admits_op_completed(const ApiEventFilter& filter, u64 op_id) noexcept;

}  // namespace reboot::client
