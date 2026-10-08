#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <vector>

#include "pending_calls.hpp"
#include "reboot/contracts/ipc.hpp"
#include "reboot/ipc/ipc_errors.hpp"

using namespace reboot;
using namespace reboot::client;

namespace {

struct Recorded {
    int runs = 0;
    std::optional<Result<Answer>> last;
};

[[nodiscard]] AnswerDone record(Recorded& recorded) {
    return [&recorded](Result<Answer> answer) {
        ++recorded.runs;
        recorded.last = std::move(answer);
    };
}

[[nodiscard]] Diagnostic lost() { return make_diag(ErrorDomain::Ipc, ipc::kConnectionLost).build(); }

}  // namespace

TEST_CASE("an answer completes its call once and later answers are dropped", "[client][calls]") {
    PendingCalls calls;
    Recorded recorded;
    const auto req_id = calls.open(record(recorded));
    REQUIRE(req_id);

    calls.answer(*req_id, contracts::ipc::Started{*req_id, 42});
    calls.answer(*req_id, contracts::ipc::Reply{*req_id, {}, {}});
    calls.fail(*req_id, lost());

    CHECK(recorded.runs == 1);
    REQUIRE(recorded.last->has_value());
    CHECK(std::get<contracts::ipc::Started>(**recorded.last).op_id == 42);
    CHECK(calls.open_count() == 0);
}

TEST_CASE("fail_all completes every open call with the reason", "[client][calls]") {
    PendingCalls calls;
    Recorded first;
    Recorded second;
    REQUIRE(calls.open(record(first)));
    REQUIRE(calls.open(record(second)));

    calls.fail_all(lost());

    for (const Recorded* recorded : {&first, &second}) {
        CHECK(recorded->runs == 1);
        REQUIRE_FALSE(recorded->last->has_value());
        CHECK(recorded->last->error().is(ipc::kConnectionLost));
    }
    CHECK(calls.open_count() == 0);
}

TEST_CASE("past kMaxOutstandingCalls a call fails at once with ipc.too_many_calls", "[client][calls]") {
    PendingCalls calls;
    std::vector<Recorded> open(contracts::ipc::kMaxOutstandingCalls);
    for (Recorded& recorded : open) REQUIRE(calls.open(record(recorded)));

    Recorded refused;
    CHECK_FALSE(calls.open(record(refused)));
    CHECK(refused.runs == 1);
    REQUIRE_FALSE(refused.last->has_value());
    CHECK(refused.last->error().is(ipc::kTooManyCalls));
    CHECK(calls.open_count() == contracts::ipc::kMaxOutstandingCalls);
}
