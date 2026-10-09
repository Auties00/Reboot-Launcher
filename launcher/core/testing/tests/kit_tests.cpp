#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "reboot/contracts/common.hpp"
#include "reboot/foundation/diag.hpp"
#include "reboot/foundation/framing.hpp"
#include "reboot/foundation/types.hpp"
#include "reboot/testing/conformance_report.hpp"
#include "reboot/testing/fake_random.hpp"
#include "reboot/testing/fault_plan.hpp"
#include "reboot/testing/frame_log.hpp"
#include "reboot/testing/golden.hpp"

using namespace rb;
using namespace rb::testing;

namespace {

enum class Op : u8 { Read, Write };

[[nodiscard]] Diagnostic boom() { return make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}); }

}  // namespace

TEST_CASE("FaultPlan arms nothing for a count of zero", "[testing][fault_plan]") {
    FaultPlan<Op> plan;
    plan.fail_next(Op::Read, boom(), 0);
    CHECK_FALSE(plan.take(Op::Read));

    plan.fail_next(Op::Read, boom(), 2);
    CHECK(plan.take(Op::Read));
    CHECK(plan.take(Op::Read));
    CHECK_FALSE(plan.take(Op::Read));
    CHECK_FALSE(plan.take(Op::Write));
}

TEST_CASE("FrameLog surfaces a malformed newest frame instead of an older one", "[testing][frame_log]") {
    FrameLog log(kChildFrameCap);
    std::vector<u8> bytes = encode_contract_frame(contracts::common::Log{LogLevel{}, 1, "first"});
    // A Log frame whose payload is a lone varint field tag with no value.
    const std::vector<u8> broken{static_cast<u8>(contract_frame_type_v<contracts::common::Log>), 0x01, 0x1A};
    bytes.insert(bytes.end(), broken.begin(), broken.end());
    REQUIRE(log.feed(bytes));
    REQUIRE(log.count(contract_frame_type_v<contracts::common::Log>) == 2);

    const auto last = log.last<contracts::common::Log>();
    REQUIRE(last);
    CHECK_FALSE(last->has_value());
    CHECK_FALSE(log.all<contracts::common::Log>().has_value());
}

TEST_CASE("ConformanceReport checks Result<T> and keeps blocked checks apart", "[testing][report]") {
    ConformanceReport report("suite", ConformanceRuntime{ports::RunnerKind::Umu, "ge-proton9-20"});
    const Result<int> ok = 1;
    const Result<int> not_found = make_diag(ErrorDomain::Internal, MessageId{"internal.bug"}).kind(ErrorKind::NotFound).fail();
    CHECK(report.expect_ok("ok", ok));
    CHECK(report.expect_error("not found", not_found, ErrorKind::NotFound));
    report.block("play", "runtime");
    CHECK(report.passed());
    CHECK(report.describe().find("blocked suite/play: runtime") != std::string::npos);

    CHECK_FALSE(report.expect_error("wrong kind", not_found, ErrorKind::Conflict));
    CHECK_FALSE(report.passed());
}

TEST_CASE("golden_mismatch names the first differing byte", "[testing][golden]") {
    const std::vector<u8> expected{1, 2, 3};
    CHECK(golden_mismatch(expected, expected).empty());
    const std::vector<u8> actual{1, 9, 3, 4};
    CHECK(golden_mismatch(expected, actual).starts_with("first difference at byte 1"));
}

TEST_CASE("FakeRandom repeats per seed", "[testing][random]") {
    FakeRandom a(7);
    FakeRandom b(7);
    std::vector<u8> first(13);
    std::vector<u8> second(13);
    a.fill(first);
    b.fill(second);
    CHECK(first == second);
}
