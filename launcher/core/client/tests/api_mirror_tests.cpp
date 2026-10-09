#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "api_event_filter.hpp"
#include "api_event_kind.hpp"
#include "api_event_payload.hpp"
#include "api_outcome.hpp"
#include "messages.hpp"
#include "reboot/ipc/ipc_errors.hpp"

using namespace rb;
using namespace rb::client;

// Golden bytes: what protoc encodes for the same reboot.api.v1 messages.
TEST_CASE("a failed Outcome encodes as reboot.api.v1 Outcome", "[client][mirror]") {
    const Diagnostic lost = make_diag(ErrorDomain::Ipc, ipc::kConnectionLost).kind(ErrorKind::EngineUnavailable).retryable();
    const std::vector<u8> expected{0x08, 0x07,                    // op_id = 7
                                   0x10, 0x81, 0x80, 0x04,        // method_id = 0x10001
                                   0x22, 0x19,                    // failed: Diagnostic, 25 bytes
                                   0x0a, 0x13, 'i', 'p', 'c', '.', 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n',
                                   '_', 'l', 'o', 's', 't',       // id
                                   0x30, 0x01,                    // retryable
                                   0x38, 0x04};                   // kind = ENGINE_UNAVAILABLE
    CHECK(encode_failed_outcome(7, 0x10001, lost) == expected);
}

TEST_CASE("the library's OpCompleted carries the Outcome as EventPayload.op_completed", "[client][mirror]") {
    const contracts::ipc::WireEvent event = op_completed_event(3, 9, std::vector<u8>{0x08, 0x09});
    CHECK(event.kind == static_cast<u32>(ApiEventKind::OpCompleted));
    CHECK(event.epoch == 3);
    CHECK(event.op == 9u);
    CHECK_FALSE(event.session);
    CHECK(event.payload == std::vector<u8>{0x12, 0x02, 0x08, 0x09});
}

TEST_CASE("an EventFilter decodes its kinds, session presence and op", "[client][mirror]") {
    // kinds = [OP_COMPLETED, RESYNC] packed, session = {}, op_id = 9.
    const std::vector<u8> bytes{0x0a, 0x02, 0x05, 0x01, 0x12, 0x00, 0x18, 0x09};
    const auto filter = decode_event_filter(bytes);
    REQUIRE(filter);
    CHECK(filter->kinds == std::vector<u32>{5, 1});
    CHECK(filter->session.has_value());
    CHECK(filter->op_id == 9u);

    const auto empty = decode_event_filter({});
    REQUIRE(empty);
    CHECK(admits_op_completed(*empty, 4));

    CHECK(decode_event_filter(std::vector<u8>{0x00}).error().is(msg::kInvalidArgument));
}

TEST_CASE("a filter admits an op's OpCompleted by kind and op, never with a session", "[client][mirror]") {
    const auto op_completed = static_cast<u32>(ApiEventKind::OpCompleted);
    CHECK(admits_op_completed(ApiEventFilter{{op_completed}, {}, {}}, 4));
    CHECK_FALSE(admits_op_completed(ApiEventFilter{{static_cast<u32>(ApiEventKind::Resync)}, {}, {}}, 4));
    CHECK(admits_op_completed(ApiEventFilter{{}, {}, 4}, 4));
    CHECK_FALSE(admits_op_completed(ApiEventFilter{{}, {}, 5}, 4));
    CHECK_FALSE(admits_op_completed(ApiEventFilter{{}, std::vector<u8>{}, {}}, 4));
}
