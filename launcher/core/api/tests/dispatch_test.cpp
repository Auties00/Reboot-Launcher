#include <catch2/catch_test_macros.hpp>

#include <any>

#include "reboot/api/v1/dispatch.hpp"
#include "reboot/api/v1/method_table.hpp"
#include "reboot/api/v1/play.hpp"

namespace api = reboot::api;

TEST_CASE("an Operation<void> completes a method whose response has no fields", "[dispatch]") {
    const auto stopped = api::encode_op_result(api::kSessionsStop, std::any{});
    REQUIRE(stopped);
    CHECK(stopped->empty());
    CHECK_FALSE(api::encode_op_result(api::kPlayStart, std::any{}));
}

TEST_CASE("an operation's response encodes as its message", "[dispatch]") {
    api::PlayStartResponse response;
    response.session.uuid.bytes[0] = 7;
    const auto bytes = api::encode_op_result(api::kPlayStart, std::any{response});
    REQUIRE(bytes);
    CHECK(*bytes == api::encode(response));
}
