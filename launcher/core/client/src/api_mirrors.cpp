#include <algorithm>
#include <utility>

#include "api_event_filter.hpp"
#include "api_event_kind.hpp"
#include "api_event_payload.hpp"
#include "api_outcome.hpp"
#include "messages.hpp"
#include "wire/codec.hpp"

namespace reboot::client {

Result<ApiEventFilter> decode_event_filter(std::span<const u8> bytes) {
    ApiEventFilter filter;
    if (!sb::wire::decode(bytes, filter))
        return make_diag(ErrorDomain::Client, msg::kInvalidArgument)
            .arg("name", "filter")
            .kind(ErrorKind::InvalidInput)
            .fail();
    return filter;
}

bool admits_op_completed(const ApiEventFilter& filter, u64 op_id) noexcept {
    if (filter.session) return false;
    if (filter.op_id && *filter.op_id != op_id) return false;
    const auto op_completed = static_cast<u32>(ApiEventKind::OpCompleted);
    return filter.kinds.empty() || std::ranges::find(filter.kinds, op_completed) != filter.kinds.end();
}

std::vector<u8> encode_failed_outcome(u64 op_id, u32 method_id, const Diagnostic& reason) {
    return sb::wire::encode_to_bytes(ApiOutcome{op_id, method_id, std::nullopt, contracts::common::to_wire(reason)});
}

contracts::ipc::WireEvent op_completed_event(u64 epoch, u64 op_id, std::span<const u8> outcome) {
    contracts::ipc::WireEvent event = library_event(ApiEventKind::OpCompleted, epoch);
    event.op = op_id;
    event.payload = sb::wire::encode_to_bytes(ApiEventPayload{std::nullopt, std::vector<u8>{outcome.begin(), outcome.end()}});
    return event;
}

}  // namespace reboot::client
