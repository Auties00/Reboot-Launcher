#include <catch2/catch_test_macros.hpp>
#include <variant>
#include <vector>

#include "op_table.hpp"

using namespace rb;
using namespace rb::client;

TEST_CASE("an op is pending until its first outcome, which then never changes", "[client][ops]") {
    OpTable ops;
    CHECK(std::holds_alternative<OpUnknown>(ops.state(7)));
    ops.track(7, 0x10001, 1);
    CHECK(std::holds_alternative<OpPending>(ops.state(7)));

    CHECK(ops.complete(7, 1, {1, 2}));
    // A replay after a same-epoch reconnect.
    CHECK_FALSE(ops.complete(7, 1, {3}));
    const OpState state = ops.state(7);
    REQUIRE(std::holds_alternative<std::vector<u8>>(state));
    CHECK(std::get<std::vector<u8>>(state) == std::vector<u8>{1, 2});
}

TEST_CASE("an untracked op takes no outcome and cannot be released", "[client][ops]") {
    OpTable ops;
    CHECK_FALSE(ops.complete(9, 1, {1}));
    CHECK_FALSE(ops.release(9));
}

TEST_CASE("pending lists only ops without an outcome, with their method", "[client][ops]") {
    OpTable ops;
    ops.track(1, 0x10001, 1);
    ops.track(2, 0, 1);
    // Tracking again keeps the first method.
    CHECK_FALSE(ops.track(1, 0x20002, 1));
    REQUIRE(ops.complete(2, 1, {}));

    const std::vector<PendingOp> pending = ops.pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].op_id == 1);
    CHECK(pending[0].method_id == 0x10001);
    CHECK(pending[0].epoch == 1);

    CHECK(ops.release(1));
    CHECK(ops.pending().empty());
    CHECK(std::holds_alternative<OpUnknown>(ops.state(1)));
}

TEST_CASE("an op of another engine epoch with the same id is a different op", "[client][ops]") {
    OpTable ops;
    ops.track(5, 0x10001, 1);
    REQUIRE(ops.complete(5, 1, {1}));
    ops.track(6, 0x10001, 1);

    // The new engine numbers its ops afresh: its op 5 replaces the old one's ended entry.
    CHECK(ops.track(5, 0x20002, 2));
    CHECK(std::holds_alternative<OpPending>(ops.state(5)));
    // A pending op of the old engine is replaced too, and its late failure no longer applies.
    CHECK(ops.track(6, 0x20002, 2));
    CHECK_FALSE(ops.complete(6, 1, {9}));
    CHECK(std::holds_alternative<OpPending>(ops.state(6)));

    REQUIRE(ops.complete(5, 2, {2}));
    const OpState state = ops.state(5);
    REQUIRE(std::holds_alternative<std::vector<u8>>(state));
    CHECK(std::get<std::vector<u8>>(state) == std::vector<u8>{2});
    const std::vector<PendingOp> pending = ops.pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending[0].op_id == 6);
    CHECK(pending[0].method_id == 0x20002);
    CHECK(pending[0].epoch == 2);
}
