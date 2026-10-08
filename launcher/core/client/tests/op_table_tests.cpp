#include <catch2/catch_test_macros.hpp>
#include <variant>
#include <vector>

#include "op_table.hpp"

using namespace reboot;
using namespace reboot::client;

TEST_CASE("an op is pending until its first outcome, which then never changes", "[client][ops]") {
    OpTable ops;
    CHECK(std::holds_alternative<OpUnknown>(ops.state(7)));
    ops.track(7, 0x10001);
    CHECK(std::holds_alternative<OpPending>(ops.state(7)));

    CHECK(ops.complete(7, {1, 2}));
    // A replay after a same-epoch reconnect.
    CHECK_FALSE(ops.complete(7, {3}));
    const OpState state = ops.state(7);
    REQUIRE(std::holds_alternative<std::vector<u8>>(state));
    CHECK(std::get<std::vector<u8>>(state) == std::vector<u8>{1, 2});
}

TEST_CASE("an untracked op takes no outcome and cannot be released", "[client][ops]") {
    OpTable ops;
    CHECK_FALSE(ops.complete(9, {1}));
    CHECK_FALSE(ops.release(9));
}

TEST_CASE("pending lists only ops without an outcome, with their method", "[client][ops]") {
    OpTable ops;
    ops.track(1, 0x10001);
    ops.track(2, 0);
    // Tracking again keeps the first method.
    ops.track(1, 0x20002);
    REQUIRE(ops.complete(2, {}));

    const std::vector<PendingOp> pending = ops.pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].op_id == 1);
    CHECK(pending[0].method_id == 0x10001);

    CHECK(ops.release(1));
    CHECK(ops.pending().empty());
    CHECK(std::holds_alternative<OpUnknown>(ops.state(1)));
}
